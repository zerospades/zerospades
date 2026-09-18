/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "EditorUI.h"
#include "KV6EditorView.h"
#include "KV6DrawTool.h"
#include "KV6EditorTool.h"
#include "KV6ScreenHelper.h"
#include "KV6SubTool.h"
#include "KV6ToolRegistry.h"
#include <Gui/UI/Components/ColorPicker.h>
#include <Gui/UI/Components/Gizmo/GizmoCanvas.h>
#include <Gui/UI/Components/Gizmo/TransformGizmo.h>
#include <Gui/UI/Components/OptionBar.h>
#include <Gui/UI/Components/Toolbar.h>
#include <Gui/UIWidgetPainter.h>
#include <Gui/OverlayPaint.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <exception>

#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IImage.h>
#include <Client/IModel.h>
#include <Client/ScreenShot.h>
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Core/LocalFileSystem.h>
#include <Core/VoxelModel.h>
#include <Gui/UI/Components/FileBrowser/FileBrowserDialog.h>
#include <Gui/UI/Components/UIOverlayHost.h>
#include <Gui/UI/Widgets/MessageBox.h>

SPADES_SETTING(cg_keyMoveForward);
SPADES_SETTING(cg_keyMoveBackward);
SPADES_SETTING(cg_keyMoveLeft);
SPADES_SETTING(cg_keyMoveRight);
SPADES_SETTING(cg_keyJump);
SPADES_SETTING(cg_keyCrouch);
SPADES_SETTING(cg_keySprint);
SPADES_SETTING(cg_keyScreenshot);

DEFINE_SPADES_SETTING(cg_keyDelete, "Delete");

namespace spades {
	namespace gui {
		namespace {
			// Mirror the in-game key matching (handles the "space" aliases) so the
			// editor honours the player's movement bindings.
			bool KV6CheckKey(const std::string& cfg, const std::string& input) {
				if (cfg.empty())
					return false;
				if (EqualsIgnoringCase(cfg, "space") || EqualsIgnoringCase(cfg, "spacebar") ||
				    EqualsIgnoringCase(cfg, "spacekey"))
					return input == " ";
				return EqualsIgnoringCase(cfg, input);
			}

			// Keys that form an editor shortcut together with Ctrl.
			bool IsCtrlShortcut(const std::string& key) {
				// Ctrl is also the descend key, so a chord here costs the movement
				// key bound to the same letter: keep this set clear of them.
				for (const char* k : {"s", "c", "x", "v", "z", "y"}) {
					if (EqualsIgnoringCase(key, k))
						return true;
				}
				return false;
			}

			float Clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

			// `v` in whole half-voxel steps, to the nearest step with ties going up.
			// A mirror plane `k` half steps from the origin maps voxel i to k - i,
			// and the plane snaps to the same count, so snapping never changes
			// which voxels a plane pairs up.
			int HalfSteps(float v) { return int(std::floor(v * 2.0F + 0.5F)); }

			// Camera speed factor while the sprint key (cg_keySprint) is held.
			constexpr float kSprintMultiplier = 3.0F;

			// How long a descend key that doubles as a chord modifier (Ctrl, the
			// default cg_keyCrouch) must be held alone before it moves the camera.
			// Long enough that even an unhurried Ctrl+Z never nudges the view, while
			// a deliberate hold still starts descending without feeling stuck.
			constexpr float kDescendGracePeriod = 0.4F;

			// The largest volume a model may grow to. A KV6 column is a 64-bit mask,
			// so no model can be deeper than 64 voxels.
			constexpr int kMaxModelWidth = 4096;
			constexpr int kMaxModelHeight = 4096;
			constexpr int kMaxModelDepth = 64;
			const char* const kMaxModelSizeMessage = "Reached the maximum model size";
			// Every edit that removes voxels refuses to take the last one.
			const char* const kLastVoxelMessage = "A model keeps at least one voxel";

			bool FitsModelSize(int w, int h, int d) {
				return w <= kMaxModelWidth && h <= kMaxModelHeight && d <= kMaxModelDepth;
			}

			// The editor's ground grid. It never moves: no model is deeper than
			// kMaxModelDepth, so a plane at that depth always sits below whatever is
			// being edited (voxel z spans [z-0.5, z+0.5], so the deepest possible
			// voxel ends half a voxel above it).
			constexpr float kGroundPlaneZ = float(kMaxModelDepth);
			constexpr int kGroundReach = 256; // voxels from the origin, each way
			constexpr int kGroundStep = 4;    // voxels between lines
			constexpr int kGroundMajorEvery = 4; // every Nth line is brighter

			// The far plane (and the fog that fades into it) has to sit beyond the
			// camera, or zooming out puts the whole model behind it.
			constexpr float kViewDistanceFactor = 4.0F;
			constexpr float kMinViewDistance = 1000.0F;

			// Option id of the brush swatch, which mirrors the editor's current
			// colour rather than holding a value of its own.
			const char* const kBrushColorOption = "color";

			// Edge length of the cube a new model's volume starts as.
			constexpr int kNewModelSize = 32;

			// Parts of a hint or help line; a line only ever breaks between them.
			const std::string kHintSeparator = "  |  ";

			// Splits `text` at its separators into lines no wider than `maxWidth`.
			// A part wider than that on its own gets a line to itself.
			std::vector<std::string> WrapParts(client::IFont& font, const std::string& text,
			                                   float maxWidth) {
				std::vector<std::string> lines;
				std::string line;
				std::size_t start = 0;
				while (true) {
					const std::size_t end = text.find(kHintSeparator, start);
					const std::string part = text.substr(start, end - start);
					const std::string joined = line.empty() ? part : line + kHintSeparator + part;
					if (!line.empty() && font.Measure(joined).x > maxWidth) {
						lines.push_back(line);
						line = part;
					} else {
						line = joined;
					}
					if (end == std::string::npos)
						break;
					start = end + kHintSeparator.size();
				}
				if (!line.empty())
					lines.push_back(line);
				return lines;
			}


			// Top UI bands (full width): a title ribbon above the toolbar. The 3D
			// viewport is inset below them by kBarsH.
			const float kRibbonH = 24.0F;
			const float kToolbarH = 32.0F;
			const float kSubBarH = 28.0F; // secondary toolbar (active tool's sub-tools)
			const float kBarsH = kRibbonH + kToolbarH + kSubBarH; // always-present bars

			// Pack a (non-negative) voxel coordinate into a selection-set key.
			int64_t SelKey(int x, int y, int z) {
				return (int64_t(x & 0xFFFFF) << 40) | (int64_t(y & 0xFFFFF) << 20) |
				       int64_t(z & 0xFFFFF);
			}
			void SelDecode(int64_t k, int& x, int& y, int& z) {
				x = int((k >> 40) & 0xFFFFF);
				y = int((k >> 20) & 0xFFFFF);
				z = int(k & 0xFFFFF);
			}

			// The 12 edges of an axis-aligned box spanning corners `a`..`b`, handed
			// one at a time to `emit(p, q)`. Shared by the cell/box outline overlays
			// and the viewport bounds, which differ only in how a line is drawn.
			template <class Emit>
			void BoxEdges(const Vector3& a, const Vector3& b, Emit emit) {
				for (int k = 0; k < 2; k++) {
					float z = (k == 0) ? a.z : b.z;
					emit(MakeVector3(a.x, a.y, z), MakeVector3(b.x, a.y, z));
					emit(MakeVector3(b.x, a.y, z), MakeVector3(b.x, b.y, z));
					emit(MakeVector3(b.x, b.y, z), MakeVector3(a.x, b.y, z));
					emit(MakeVector3(a.x, b.y, z), MakeVector3(a.x, a.y, z));
				}
				emit(MakeVector3(a.x, a.y, a.z), MakeVector3(a.x, a.y, b.z));
				emit(MakeVector3(b.x, a.y, a.z), MakeVector3(b.x, a.y, b.z));
				emit(MakeVector3(b.x, b.y, a.z), MakeVector3(b.x, b.y, b.z));
				emit(MakeVector3(a.x, b.y, a.z), MakeVector3(a.x, b.y, b.z));
			}

			Vector3 AxisUnit3(int a) {
				return MakeVector3(a == 0 ? 1.0F : 0.0F, a == 1 ? 1.0F : 0.0F, a == 2 ? 1.0F : 0.0F);
			}
			Vector4 AxisTint(int a) {
				if (a == 0) return MakeVector4(0.78F, 0.5F, 0.5F, 1.0F);
				if (a == 1) return MakeVector4(0.5F, 0.74F, 0.5F, 1.0F);
				return MakeVector4(0.55F, 0.58F, 0.78F, 1.0F);
			}
			const char* FaceLabel(int ax, int sg) {
				static const char* names[3][2] = {
				  {"Right", "Left"}, {"Back", "Front"}, {"Top", "Bottom"}};
				return names[ax][sg > 0 ? 1 : 0];
			}
			// Inside a convex polygon (n verts, ordered): consistent edge-cross signs.
			bool PointInPoly(const Vector2& p, const Vector2* q, int n) {
				float sign = 0.0F;
				for (int i = 0; i < n; i++) {
					const Vector2& a = q[i];
					const Vector2& b = q[(i + 1) % n];
					float cr = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
					if (i == 0) sign = cr;
					else if (cr * sign < 0.0F) return false;
				}
				return true;
			}

			// One facet of the chamfered (beveled) navigation cube.
			struct NaviFacet {
				int n;             // 3 (corner bevel) or 4 (face / edge bevel)
				Vector3 v[4];      // world-space vertices
				Vector3 dir;       // outward normal = snap view direction
				int tint;          // axis 0/1/2 for faces, -1 for bevels
				const char* label; // for the 6 faces, else nullptr
			};

			Vector3 Comp3(int ax, float on, int u, float uu, int v, float vv) {
				float c[3];
				c[ax] = on; c[u] = uu; c[v] = vv;
				return MakeVector3(c[0], c[1], c[2]);
			}

			// Build the 6 faces + 12 edge bevels + 8 corner bevels of the cube.
			void BuildNaviFacets(std::vector<NaviFacet>& out) {
				const float t = 0.66F; // face half-extent; bevels live in [t, 1]
				out.clear();
				// 6 faces (square, shrunk to +/-t).
				for (int ax = 0; ax < 3; ax++) {
					int u = (ax + 1) % 3, v = (ax + 2) % 3;
					for (int s = -1; s <= 1; s += 2) {
						NaviFacet f;
						f.n = 4;
						f.v[0] = Comp3(ax, float(s), u, -t, v, -t);
						f.v[1] = Comp3(ax, float(s), u, t, v, -t);
						f.v[2] = Comp3(ax, float(s), u, t, v, t);
						f.v[3] = Comp3(ax, float(s), u, -t, v, t);
						f.dir = AxisUnit3(ax) * float(s);
						f.tint = ax;
						f.label = FaceLabel(ax, s);
						out.push_back(f);
					}
				}
				// 12 edge bevels (one per axis-pair and sign pair).
				const int pairs[3][2] = {{0, 1}, {1, 2}, {2, 0}};
				for (int pi = 0; pi < 3; pi++) {
					int a = pairs[pi][0], b = pairs[pi][1], c = 3 - a - b;
					for (int sa = -1; sa <= 1; sa += 2)
					for (int sb = -1; sb <= 1; sb += 2) {
						NaviFacet f;
						f.n = 4;
						float c0[3], c1[3], c2[3], c3[3];
						c0[a] = float(sa); c0[b] = sb * t; c0[c] = -t;
						c1[a] = float(sa); c1[b] = sb * t; c1[c] = t;
						c2[a] = sa * t; c2[b] = float(sb); c2[c] = t;
						c3[a] = sa * t; c3[b] = float(sb); c3[c] = -t;
						f.v[0] = MakeVector3(c0[0], c0[1], c0[2]);
						f.v[1] = MakeVector3(c1[0], c1[1], c1[2]);
						f.v[2] = MakeVector3(c2[0], c2[1], c2[2]);
						f.v[3] = MakeVector3(c3[0], c3[1], c3[2]);
						f.dir = (AxisUnit3(a) * float(sa) + AxisUnit3(b) * float(sb)).Normalize();
						f.tint = -1;
						f.label = nullptr;
						out.push_back(f);
					}
				}
				// 8 corner bevels (triangles).
				for (int sx = -1; sx <= 1; sx += 2)
				for (int sy = -1; sy <= 1; sy += 2)
				for (int sz = -1; sz <= 1; sz += 2) {
					NaviFacet f;
					f.n = 3;
					f.v[0] = MakeVector3(sx * 1.0F, sy * t, sz * t);
					f.v[1] = MakeVector3(sx * t, sy * 1.0F, sz * t);
					f.v[2] = MakeVector3(sx * t, sy * t, sz * 1.0F);
					f.dir = MakeVector3(float(sx), float(sy), float(sz)).Normalize();
					f.tint = -1;
					f.label = nullptr;
					out.push_back(f);
				}
			}

			// The navigation cube is fixed geometry, so build the facet set once and
			// hand back the shared copy (queried every frame for drawing and picking).
			const std::vector<NaviFacet>& NaviFacets() {
				static const std::vector<NaviFacet> facets = [] {
					std::vector<NaviFacet> out;
					BuildNaviFacets(out);
					return out;
				}();
				return facets;
			}
		} // namespace

		KV6EditorView::KV6EditorView(client::IRenderer* r, client::IAudioDevice* dev,
		                             client::FontManager* fm, SoftwareCursor* c,
		                             const std::string& path, bool isNew)
		    : renderer(r), audioDevice(dev), fontManager(fm) {
			SPADES_MARK_FUNCTION();
			if (c) {
				softwareCursor = c;
			} else {
				ownedCursor = std::make_unique<SoftwareCursor>(*renderer);
				softwareCursor = ownedCursor.get();
			}
			io = Handle<KV6ScreenHelper>::New();

			// Initialize UI manager
			ui = Handle<EditorUI>::New(renderer.GetPointerOrNull(), audioDevice.GetPointerOrNull(),
			                           &*fontManager, this, softwareCursor);

			// Wire up toolbar callbacks. The mode buttons are the EditorMode values
			// in order; the toolbar only reports clicks on the enabled ones.
			ui->GetToolbar()->OnModeClicked = [this](int idx) {
				EditorMode mode = EditorMode(idx);
				if (ModeSupported(mode))
					SetMode(mode);
			};
			ui->GetToolbar()->OnToolClicked = [this](int idx) { SetActiveTool(idx); };
			ui->GetToolbar()->OnUndoClicked = [this]() { Undo(); };
			ui->GetToolbar()->OnRedoClicked = [this]() { Redo(); };

			// Wire up option bar callbacks
			ui->GetOptionBar()->OnSubToolClicked = [this](int idx) {
				EditorTool* tool = ActiveTool();
				if (!tool) return;
				tool->SetSubTool(*this, idx);
			};
			ui->GetOptionBar()->OnBoolToggled = [this](int idx) {
				EditorTool* tool = ActiveTool();
				if (!tool) return;
				ToolOptions* opts = tool->Options();
				if (!opts || idx < 0 || idx >= opts->Count()) return;
				const ToolOption& opt = opts->At(idx);
				if (opt.type != ToolOption::Type::Bool) return;
				opts->At(idx).bvalue = !opts->At(idx).bvalue;
				tool->OnOptionToggled(*this, opt.id, opt.bvalue);
			};
			ui->GetOptionBar()->OnActionClicked = [this](int idx) {
				EditorTool* tool = ActiveTool();
				if (!tool) return;
				ToolOptions* opts = tool->Options();
				if (!opts || idx < 0 || idx >= opts->Count()) return;
				const ToolOption& opt = opts->At(idx);
				if (opt.type != ToolOption::Type::Action) return;
				tool->OnAction(*this, opt.id);
			};
			ui->GetOptionBar()->OnColorClicked = [this](int idx) {
				EditorTool* tool = ActiveTool();
				if (!tool) return;
				ToolOptions* opts = tool->Options();
				if (!opts || idx < 0 || idx >= opts->Count()) return;
				const ToolOption& opt = opts->At(idx);
				if (opt.type != ToolOption::Type::Color) return;
				// The brush swatch edits the shared colour; any other swatch (a
				// future second colour) edits its own value, in its own tool.
				bool brush = opt.id == kBrushColorOption;
				colorTargetTool = brush ? nullptr : tool;
				colorTargetOption = brush ? std::string() : opt.id;
				ui->GetColorPicker()->SetColor(brush ? currentColor : opt.color);
				ui->GetColorPicker()->Open();
			};

			// Wire up color picker callbacks
			ui->GetColorPicker()->OnColorChanged = [this](uint32_t c) {
				// The picker stays open across tool switches, so the edit goes to the
				// swatch it was opened on, whichever tool is active now.
				if (colorTargetTool) {
					if (ToolOptions* opts = colorTargetTool->Options())
						opts->SetColor(colorTargetOption, c);
					return;
				}
				currentColor = c;
			};

			// The Edit-mode tools come from the registry (toolbar order = registration).
			// Open on Draw, so the user can start modelling; found by type, so
			// no button label decides it.
			ToolRegistry::Instance().BuildAll(tools);
			activeTool = 0;
			for (size_t i = 0; i < tools.size(); i++) {
				if (dynamic_cast<DrawTool*>(tools[i].tool.get())) {
					activeTool = int(i);
					break;
				}
			}

			if (isNew || path.empty())
				NewModel(kNewModelSize, path);
			else
				LoadModel(path);
		}

		KV6EditorView::~KV6EditorView() { SPADES_MARK_FUNCTION(); }

		// --- Document ---------------------------------------------------------

		void KV6EditorView::FrameCamera() {
			orbitTarget = MakeVector3(model->GetWidth() * 0.5F - 0.5F, model->GetHeight() * 0.5F - 0.5F,
			                          model->GetDepth() * 0.5F - 0.5F);
			orbitDist =
			  float(std::max(model->GetWidth(), std::max(model->GetHeight(), model->GetDepth()))) *
			  1.8F;
		}

		void KV6EditorView::NewModel(int n, const std::string& path) {
			DropPlacement(); // it belongs to the document being replaced
			previewedOrigin.reset();
			previewedMirrorPlane.reset();
			cubeSize = n;
			model = Handle<VoxelModel>::New(n, n, n);
			model->SetSolid(n / 2, n / 2, n / 2, currentColor);
			// Anchor the pivot on the seed voxel so mirroring (whose planes start
			// there) is symmetric about it from the start.
			float c = float(n / 2);
			model->SetOrigin(MakeVector3(-c, -c, -c));
			voxelCount = 1;
			PlaceMirrorPlane(GetPivot()); // the history starts afresh below
			RebuildRenderModel();
			filePath = path;
			FrameCamera();
			selection.clear(); // it named voxels of the previous document
			undo.Clear();
			savedGeomId = -1; // a fresh, never-saved document starts dirty
			NotifyDocumentChanged();
		}

		void KV6EditorView::LoadModel(const std::string& path) {
			DropPlacement(); // it belongs to the document being replaced
			previewedOrigin.reset();
			previewedMirrorPlane.reset();
			VoxelModel* loaded = io->Load(path);
			if (!loaded) {
				NewModel(kNewModelSize, path);
				SetStatus("Could not load " + path);
				return;
			}
			model = Handle<VoxelModel>(loaded, false); // adopt (Load returns a ref)
			cubeSize = std::max(model->GetWidth(), std::max(model->GetHeight(), model->GetDepth()));
			voxelCount = CountSolids();
			PlaceMirrorPlane(GetPivot()); // the history starts afresh below
			RebuildRenderModel();
			filePath = path;
			FrameCamera();
			selection.clear(); // it named voxels of the previous document
			undo.Clear();
			savedGeomId = undo.GeometryStateId(); // a freshly loaded document is clean
			NotifyDocumentChanged();
		}

		int KV6EditorView::CountSolids() {
			int count = 0;
			for (int x = 0; x < model->GetWidth(); x++)
			for (int y = 0; y < model->GetHeight(); y++)
			for (int z = 0; z < model->GetDepth(); z++) {
				if (model->IsSolid(x, y, z))
					count++;
			}
			return count;
		}

		bool KV6EditorView::InBounds(int x, int y, int z) const {
			return x >= 0 && y >= 0 && z >= 0 && x < model->GetWidth() && y < model->GetHeight() &&
			       z < model->GetDepth();
		}

		void KV6EditorView::RebuildRenderModel() { renderModel = renderer->CreateModel(*model); }

		void KV6EditorView::SetStatus(const std::string& s) {
			statusMessage = s;
			statusTimer = 2.5F;
		}

		bool KV6EditorView::Save() {
			// Saving writes the document, so anything still pending belongs in it.
			DocumentCommand command(*this);
			if (filePath.empty()) {
				SetStatus("No file to save to");
				return false;
			}
			if (!io->Save(&*model, filePath)) {
				SetStatus("Save failed");
				return false;
			}
			savedGeomId = undo.GeometryStateId(); // this geometry state is now clean
			SetStatus("Saved " + filePath);
			return true;
		}

		std::vector<EditorMenuItem> KV6EditorView::GetMenuItems() {
			auto newModel = [this] {
				ConfirmDiscardChanges([this] {
					NewModel(kNewModelSize, std::string());
					SetStatus("New model");
				});
			};
			std::vector<EditorMenuItem> items;
			items.push_back(EditorMenuItem{"New Model", newModel, true});
			items.push_back(EditorMenuItem{"Open Model...", [this] { OpenDocument(); }, true});
			items.push_back(EditorMenuItem{"Import Model...", [this] { OpenImportDialog(); }, true});
			items.push_back(EditorMenuItem{"Save", [this] { Save(); }, !filePath.empty()});
			items.push_back(EditorMenuItem{"Save As...", [this] { OpenSaveAsDialog(); }, true});
			items.push_back(EditorMenuItem{
			  "Exit to Menu", [this] { ConfirmDiscardChanges([this] { wantsClose = true; }); }, true});
			return items;
		}

		void KV6EditorView::ConfirmDiscardChanges(std::function<void()> proceed) {
			if (!proceed)
				return;
			if (!HasUnsavedChanges()) {
				proceed();
				return;
			}

			UIOverlayHost* overlay = ui->GetOverlay();
			Handle<MessageBoxScreen> box = Handle<MessageBoxScreen>::New(
			  &overlay->GetUIManager().GetRootElement(),
			  "This model has unsaved changes.",
			  std::vector<std::string>{"Save", "Discard", "Cancel"}, 160.0F);
			box->closed = [this, proceed](ui::UIElement& sender) {
				MessageBoxScreen* answer = dynamic_cast<MessageBoxScreen*>(&sender);
				if (!answer)
					return;
				if (answer->resultIndex == 1) { // Discard
					proceed();
				} else if (answer->resultIndex == 0) { // Save
					// A document that was never saved needs somewhere to go first;
					// `proceed` then runs only if that save succeeds.
					if (filePath.empty())
						OpenSaveAsDialog(proceed);
					else if (Save())
						proceed();
				}
			};
			ReleaseHeldInput();
			overlay->Show(box.GetPointerOrNull());
		}

		void KV6EditorView::OpenDocument() {
			ConfirmDiscardChanges([this] {
				FileBrowserOptions options;
				options.purpose = FileBrowserPurpose::Open;
				options.target = FileBrowserTarget::Files;
				options.homeDir = io->DefaultDir();
				options.initialDir =
				  filePath.empty() ? io->DefaultDir() : LocalFileSystem::ParentDir(filePath);
				options.filters.push_back(FileFilter{"KV6 models", {GetDocumentExtension()}});

				UIOverlayHost* overlay = ui->GetOverlay();
				Handle<FileBrowserDialog> dialog = Handle<FileBrowserDialog>::New(
				  &overlay->GetUIManager().GetRootElement(), "Open Model", std::move(options));
				dialog->closed = [this](const FileBrowserResult& result) {
					if (result.accepted && !result.paths.empty())
						LoadModel(result.paths.front());
				};
				ReleaseHeldInput();
				overlay->Show(dialog.GetPointerOrNull());
			});
		}

		void KV6EditorView::OpenSaveAsDialog(std::function<void()> after) {
			FileBrowserOptions options;
			options.purpose = FileBrowserPurpose::Save;
			options.target = FileBrowserTarget::Files;
			options.homeDir = io->DefaultDir();
			options.initialDir =
			  filePath.empty() ? io->DefaultDir() : LocalFileSystem::ParentDir(filePath);
			options.initialName =
			  filePath.empty() ? "untitled.kv6" : LocalFileSystem::GetFileName(filePath);
			options.filters.push_back(
			  FileFilter{"KV6 models", {GetDocumentExtension()}});

			UIOverlayHost* overlay = ui->GetOverlay();
			Handle<FileBrowserDialog> dialog = Handle<FileBrowserDialog>::New(
			  &overlay->GetUIManager().GetRootElement(), "Save As", std::move(options));
			dialog->closed = [this, after](const FileBrowserResult& result) {
				if (!result.accepted || result.paths.empty())
					return;
				if (SaveDocument(result.paths.front()) && after)
					after();
			};
			// A dialog takes over input: nothing should stay held while it is up.
			ReleaseHeldInput();
			overlay->Show(dialog.GetPointerOrNull());
		}

		void KV6EditorView::OpenImportDialog() {
			FileBrowserOptions options;
			options.purpose = FileBrowserPurpose::Open;
			options.target = FileBrowserTarget::Files;
			options.homeDir = io->DefaultDir();
			options.initialDir =
			  filePath.empty() ? io->DefaultDir() : LocalFileSystem::ParentDir(filePath);
			options.filters.push_back(FileFilter{"KV6 models", {GetDocumentExtension()}});

			UIOverlayHost* overlay = ui->GetOverlay();
			Handle<FileBrowserDialog> dialog = Handle<FileBrowserDialog>::New(
			  &overlay->GetUIManager().GetRootElement(), "Import Model", std::move(options));
			dialog->closed = [this](const FileBrowserResult& result) {
				if (result.accepted && !result.paths.empty())
					ImportModel(result.paths.front());
			};
			ReleaseHeldInput();
			overlay->Show(dialog.GetPointerOrNull());
		}

		bool KV6EditorView::SaveDocument(const std::string& path) {
			filePath = path;
			return Save();
		}

		bool KV6EditorView::OnMenuEscape() {
			// Escape backs out one level per press, the same in every tool: the
			// gesture in progress, then pending voxels, the eyedropper, the colour
			// picker and the selection. The menu only opens once nothing is left.
			// (The menu asks before opening, so this runs first.)
			if (EditorTool* t = ActiveTool()) {
				if (t->OnEscape(*this))
					return true;
			}
			// A placement left pending by a tool that does not handle Escape itself.
			if (placementActive) {
				CancelPlacement();
				return true;
			}
			ColorPicker& picker = *ui->GetColorPicker();
			if (picker.GetEyedropperMode()) {
				picker.SetEyedropperMode(false);
				return true;
			}
			if (picker.IsOpen()) {
				picker.Close();
				return true;
			}
			if (!selection.empty()) {
				ClearSelection();
				SetStatus("Selection cleared");
				return true;
			}
			return false;
		}

		// --- Camera -----------------------------------------------------------

		Vector3 KV6EditorView::Forward() const {
			float cp = cosf(pitch);
			return MakeVector3(cp * cosf(yaw), cp * sinf(yaw), -sinf(pitch));
		}

		Vector3 KV6EditorView::CameraEye() const { return orbitTarget - Forward() * orbitDist; }

		float KV6EditorView::ViewDistance() const {
			// Far enough to keep the model visible at any zoom, and never nearer than
			// the fixed distance the editor used before.
			return std::max(kMinViewDistance, orbitDist * kViewDistanceFactor);
		}

		void KV6EditorView::UpdateMovement(float dt) {
			Vector3 fwd = Forward();
			Vector3 up = MakeVector3(0.0F, 0.0F, -1.0F);
			// Cross(fwd, up) normalised, derived from the heading so it stays valid
			// when looking straight up/down (navicube top/bottom), where the cross
			// product collapses. Matches the camera side axis in SetupScene.
			Vector3 right = MakeVector3(-sinf(yaw), cosf(yaw), 0.0F);

			float step = float(cubeSize) * 0.7F * (keySprint ? kSprintMultiplier : 1.0F) * dt;
			bool descending = keyDown && DescendKeyIsActive();
			auto displacement = [&](bool withDescend) {
				Vector3 move = MakeVector3(0, 0, 0);
				if (keyFwd) move += fwd;
				if (keyBack) move -= fwd;
				if (keyRight) move += right;
				if (keyLeft) move -= right;
				if (keyUp) move += up;
				if (withDescend && descending) move -= up;
				if (move.x == 0.0F && move.y == 0.0F && move.z == 0.0F)
					return MakeVector3(0, 0, 0);
				return move.Normalize() * step;
			};

			Vector3 delta = displacement(true);
			// Moving the orbited point carries the whole view along with it.
			orbitTarget += delta;

			// While Ctrl is both the descend key and held, remember how far the
			// descend part alone moved us, so a Ctrl shortcut can take it back.
			if (descending && ctrlHeld && KV6CheckKey(cg_keyCrouch, "Control"))
				ctrlDescent += delta - displacement(false);
		}

		bool KV6EditorView::DescendKeyIsActive() const {
			// A descend key that cannot start a shortcut moves the camera at once.
			if (!KV6CheckKey(cg_keyCrouch, "Control"))
				return true;
			// Ctrl also opens Ctrl+S/C/X/V/Z/Y, so wait out the moment in which the
			// letter of a chord would arrive: the view then never moves for a
			// shortcut, and a deliberate hold still descends.
			return globalTime - descendPressTime >= kDescendGracePeriod;
		}

		void KV6EditorView::PanView(float dx, float dy) {
			if (!camera.IsValid())
				return;
			// World units per screen pixel at the orbited point, so the point under
			// the cursor follows the drag at any zoom level.
			float unitsPerPixel = camera.WorldPerPixel(orbitTarget);
			orbitTarget +=
			  camera.right * (-dx * unitsPerPixel) + camera.up * (dy * unitsPerPixel);
		}

		void KV6EditorView::ReleaseHeldInput() {
			keyFwd = keyBack = keyLeft = keyRight = keyUp = keyDown = false;
			ctrlDescent = MakeVector3(0, 0, 0);
			lookActive = false;
			// The release of a held button may be swallowed too, so the press it
			// began is over: the tool and the colour picker drop their drags and
			// nothing reads as dragged.
			lmbHeld = rmbHeld = false;
			if (ui)
				ui->GetColorPicker()->MouseUp();
			CancelToolInteraction();
		}

		client::SceneDefinition KV6EditorView::SetupScene(float vpX, float vpY, float vpW, float vpH) {
			client::SceneDefinition sceneDef;
			Vector3 eye = CameraEye();
			Vector3 at = orbitTarget;
			Vector3 up = MakeVector3(0.0F, 0.0F, -1.0F);

			Vector3 dir = (at - eye).Normalize();
			// At the navicube Top/Bottom views dir is parallel to the world-up
			// reference, so Cross(dir, up) collapses to NaN. For every non-vertical
			// pitch Cross(dir, up).Normalize() equals (-sin(yaw), cos(yaw), 0); use
			// that heading-derived value at the pole too. It is the continuous limit
			// of the cross product, so the camera reaches top/bottom smoothly instead
			// of snapping, and only flips if rotated past the pole.
			Vector3 side;
			if (std::fabs(Vector3::Dot(dir, up)) > 0.999F)
				side = MakeVector3(-sinf(yaw), cosf(yaw), 0.0F);
			else
				side = Vector3::Cross(dir, up).Normalize();
			up = -Vector3::Cross(dir, side);

			sceneDef.viewOrigin = eye;
			sceneDef.viewAxis[0] = side;
			sceneDef.viewAxis[1] = up;
			sceneDef.viewAxis[2] = dir;
			sceneDef.fovY = 60.0F * M_PI_F / 180.0F;
			sceneDef.fovX = 2.0F * atanf(tanf(sceneDef.fovY * 0.5F) * (vpW / vpH));
			sceneDef.zNear = 0.1F;
			sceneDef.zFar = ViewDistance();
			sceneDef.viewportLeft = int(vpX);
			sceneDef.viewportTop = int(vpY);
			sceneDef.viewportWidth = int(vpW);
			sceneDef.viewportHeight = int(vpH);
			// The editor draws its own ground: the game world brings chunk culling
			// and terrain that crowds the model, and water is a plane following the
			// camera at sea level with a mirrored scene rendered into it.
			sceneDef.skipWorld = true;
			sceneDef.skipWater = true;
			// A voxel has six face normals, so sunlight splits one palette colour
			// into several on-screen shades and the model stops reading as the
			// colour it was painted. Light it evenly instead, and outline it so the
			// shape survives the loss of shading.
			sceneDef.flatModelLighting = true;
			sceneDef.forceOutlines = true;
			sceneDef.denyCameraBlur = true;
			sceneDef.time = (unsigned int)(globalTime * 1000.0F);
			return sceneDef;
		}

		// --- Editing ----------------------------------------------------------

		bool KV6EditorView::RayPlaneCell(const Vector3& planePoint, const Vector3& normal,
		                                 IntVector3& out) {
			if (!camera.IsValid())
				return false;
			Vector3 origin, dir;
			camera.Ray(softwareCursor->GetPosition(), origin, dir);
			float denom = Vector3::Dot(dir, normal);
			if (std::fabs(denom) < 1.0e-5F)
				return false;
			float t = Vector3::Dot(planePoint - origin, normal) / denom;
			if (t <= 0.0F)
				return false;
			Vector3 hit = origin + dir * t;
			out = MakeIntVector3(int(std::floor(hit.x + 0.5F)), int(std::floor(hit.y + 0.5F)),
			                     int(std::floor(hit.z + 0.5F)));
			return true;
		}

		Vector2 KV6EditorView::WorldToScreen(const Vector3& w, bool& ok) const {
			Vector2 screen = MakeVector2(0.0F, 0.0F);
			ok = camera.Project(w, screen);
			return screen;
		}

		// A bright depth-tested 3D line, also collected for the dim see-through pass.
		void KV6EditorView::EmitLine(const Vector3& a, const Vector3& b, const Vector4& color) {
			renderer->AddDebugLine(a, b, color);
			overlayLines.push_back({a, b, color});
		}

		void KV6EditorView::DrawLine3D(const Vector3& a, const Vector3& b, const Vector4& color) {
			EmitLine(a, b, color);
		}

		// Re-draw the collected overlay lines as dim 2D lines (always on top), so the
		// parts hidden behind voxels still show. Cleared each frame.
		void KV6EditorView::DrawOverlayLines2D() {
			for (const OverlayLine& l : overlayLines) {
				bool ok1, ok2;
				Vector2 pa = WorldToScreen(l.a, ok1);
				Vector2 pb = WorldToScreen(l.b, ok2);
				if (!ok1 || !ok2)
					continue; // endpoint behind the camera
				Vector4 c = l.color;
				c.w *= 0.35F; // dimmer than the visible (depth-tested) line
				DrawLine2D(pa, pb, 1.0F, c);
			}
			overlayLines.clear();
		}

		void KV6EditorView::DoPick() {
			pickHit = false;
			if (!camera.IsValid())
				return;

			Vector3 eye, dir;
			camera.Ray(softwareCursor->GetPosition(), eye, dir);

			// The renderer centres voxel (x,y,z) at world (x,y,z), so traverse in a
			// +0.5-shifted space where each integer cell maps to a voxel index.
			Vector3 o = eye + MakeVector3(0.5F, 0.5F, 0.5F);

			int cx = int(std::floor(o.x));
			int cy = int(std::floor(o.y));
			int cz = int(std::floor(o.z));

			int stepX = (dir.x >= 0.0F) ? 1 : -1;
			int stepY = (dir.y >= 0.0F) ? 1 : -1;
			int stepZ = (dir.z >= 0.0F) ? 1 : -1;

			float big = 1.0e30F;
			float tDeltaX = (dir.x != 0.0F) ? std::fabs(1.0F / dir.x) : big;
			float tDeltaY = (dir.y != 0.0F) ? std::fabs(1.0F / dir.y) : big;
			float tDeltaZ = (dir.z != 0.0F) ? std::fabs(1.0F / dir.z) : big;

			float tMaxX = (dir.x != 0.0F)
			  ? ((float(cx) + (stepX > 0 ? 1.0F : 0.0F) - o.x) / dir.x) : big;
			float tMaxY = (dir.y != 0.0F)
			  ? ((float(cy) + (stepY > 0 ? 1.0F : 0.0F) - o.y) / dir.y) : big;
			float tMaxZ = (dir.z != 0.0F)
			  ? ((float(cz) + (stepZ > 0 ? 1.0F : 0.0F) - o.z) / dir.z) : big;

			int px = cx, py = cy, pz = cz;
			int limit = (model->GetWidth() + model->GetHeight() + model->GetDepth()) * 3 + 8;
			for (int i = 0; i < limit; i++) {
				if (InBounds(cx, cy, cz) && model->IsSolid(cx, cy, cz)) {
					pickHit = true;
					pickHX = cx; pickHY = cy; pickHZ = cz;
					pickPX = px; pickPY = py; pickPZ = pz;
					return;
				}
				px = cx; py = cy; pz = cz;
				if (tMaxX <= tMaxY && tMaxX <= tMaxZ) {
					cx += stepX; tMaxX += tDeltaX;
				} else if (tMaxY <= tMaxZ) {
					cy += stepY; tMaxY += tDeltaY;
				} else {
					cz += stepZ; tMaxZ += tDeltaZ;
				}
			}
		}

		int KV6EditorView::MirrorIdx(int i, float plane) const { return HalfSteps(plane) - i; }

		void KV6EditorView::ExpandMirrors(std::vector<IntVector3>& cells) const {
			bool mx = MirrorOn(0), my = MirrorOn(1), mz = MirrorOn(2);
			if (!mx && !my && !mz)
				return;
			std::vector<IntVector3> out;
			out.reserve(cells.size() * 8);
			for (const IntVector3& c : cells) {
				int xs[2] = {c.x, c.x}, nx = 1;
				int ys[2] = {c.y, c.y}, ny = 1;
				int zs[2] = {c.z, c.z}, nz = 1;
				if (mx) { int m = MirrorIdx(c.x, mirror.plane.x); if (m != c.x) { xs[1] = m; nx = 2; } }
				if (my) { int m = MirrorIdx(c.y, mirror.plane.y); if (m != c.y) { ys[1] = m; ny = 2; } }
				if (mz) { int m = MirrorIdx(c.z, mirror.plane.z); if (m != c.z) { zs[1] = m; nz = 2; } }
				for (int a = 0; a < nx; a++)
				for (int b = 0; b < ny; b++)
				for (int d = 0; d < nz; d++)
					out.push_back(MakeIntVector3(xs[a], ys[b], zs[d]));
			}
			cells.swap(out);
		}

		void KV6EditorView::RebuildVolume(int nw, int nh, int nd, int ox, int oy, int oz) {
			undo.RecordReframe(model->GetWidth(), model->GetHeight(), model->GetDepth(), nw, nh, nd,
			                   ox, oy, oz);
			ReframeRaw(nw, nh, nd, ox, oy, oz);
		}

		void KV6EditorView::ReframeRaw(int nw, int nh, int nd, int ox, int oy, int oz) {
			Handle<VoxelModel> dst = Handle<VoxelModel>::New(nw, nh, nd);
			for (int x = 0; x < model->GetWidth(); x++)
			for (int y = 0; y < model->GetHeight(); y++)
			for (int z = 0; z < model->GetDepth(); z++) {
				if (model->IsSolid(x, y, z))
					dst->SetSolid(x + ox, y + oy, z + oz, model->GetColor(x, y, z));
			}
			Vector3 shift = MakeVector3(float(ox), float(oy), float(oz));
			dst->SetOrigin(model->GetOrigin() - shift);
			model = dst;
			orbitTarget += shift;
			// The mirror planes are in voxel coordinates too. The shift is whole
			// voxels, so they stay on the half-step grid.
			mirror.plane += shift;
			cubeSize = std::max(nw, std::max(nh, nd));
			ShiftSelection(ox, oy, oz); // keep selected voxel coords aligned
			// A pending placement stores document coordinates too, so it has to
			// follow the relabelling or it would apply in the wrong place.
			if (placementActive) {
				const IntVector3 shift = MakeIntVector3(ox, oy, oz);
				placement.anchor = placement.anchor + shift;
				placement.pivot = placement.pivot + shift;
				for (IntVector3& v : placement.lifted) {
					v.x += ox;
					v.y += oy;
					v.z += oz;
				}
			}
		}

		void KV6EditorView::TrimVolume() {
			int minX = model->GetWidth(), minY = model->GetHeight(), minZ = model->GetDepth();
			int maxX = -1, maxY = -1, maxZ = -1;
			for (int x = 0; x < model->GetWidth(); x++)
			for (int y = 0; y < model->GetHeight(); y++)
			for (int z = 0; z < model->GetDepth(); z++) {
				if (!model->IsSolid(x, y, z))
					continue;
				minX = std::min(minX, x); minY = std::min(minY, y); minZ = std::min(minZ, z);
				maxX = std::max(maxX, x); maxY = std::max(maxY, y); maxZ = std::max(maxZ, z);
			}
			if (maxX < 0)
				return;
			int nw = maxX - minX + 1, nh = maxY - minY + 1, nd = maxZ - minZ + 1;
			if (nw == model->GetWidth() && nh == model->GetHeight() && nd == model->GetDepth())
				return;
			RebuildVolume(nw, nh, nd, -minX, -minY, -minZ);
		}

		void KV6EditorView::PlaceCube() {
			DocumentCommand command(*this);
			DoPick();
			if (!pickHit)
				return;
			int tx = pickPX, ty = pickPY, tz = pickPZ;

			// Target plus its mirror images across the mirror planes. The mirror —
			// or a placement past an edge — may land outside, so grow to fit them all.
			bool mx = MirrorOn(0), my = MirrorOn(1), mz = MirrorOn(2);
			int xb = mx ? MirrorIdx(tx, mirror.plane.x) : tx; int nx = (mx && xb != tx) ? 2 : 1;
			int yb = my ? MirrorIdx(ty, mirror.plane.y) : ty; int ny = (my && yb != ty) ? 2 : 1;
			int zb = mz ? MirrorIdx(tz, mirror.plane.z) : tz; int nz = (mz && zb != tz) ? 2 : 1;

			int loX = std::min(0, tx); int hiX = std::max(model->GetWidth(), tx + 1);
			if (nx == 2) { loX = std::min(loX, xb); hiX = std::max(hiX, xb + 1); }
			int loY = std::min(0, ty); int hiY = std::max(model->GetHeight(), ty + 1);
			if (ny == 2) { loY = std::min(loY, yb); hiY = std::max(hiY, yb + 1); }
			int loZ = std::min(0, tz); int hiZ = std::max(model->GetDepth(), tz + 1);
			if (nz == 2) { loZ = std::min(loZ, zb); hiZ = std::max(hiZ, zb + 1); }

			int nw = hiX - loX, nh = hiY - loY, nd = hiZ - loZ;
			if (!FitsModelSize(nw, nh, nd)) {
				SetStatus(kMaxModelSizeMessage);
				return;
			}
			int ox = -loX, oy = -loY, oz = -loZ;
			KV6UndoStack::Step step(undo, "Place");
			if (ox != 0 || oy != 0 || oz != 0 || nw != model->GetWidth() ||
			    nh != model->GetHeight() || nd != model->GetDepth())
				RebuildVolume(nw, nh, nd, ox, oy, oz);
			tx += ox; xb += ox; ty += oy; yb += oy; tz += oz; zb += oz;

			bool any = false;
			for (int ia = 0; ia < nx; ia++) { int X = (ia == 0) ? tx : xb;
			for (int ib = 0; ib < ny; ib++) { int Y = (ib == 0) ? ty : yb;
			for (int ic = 0; ic < nz; ic++) { int Z = (ic == 0) ? tz : zb;
				if (!model->IsSolid(X, Y, Z)) {
					WriteVoxel(X, Y, Z, true, currentColor);
					any = true;
				}
			}}}
			if (any)
				RebuildRenderModel();
		}

		void KV6EditorView::DeleteCube() {
			DocumentCommand command(*this);
			DoPick();
			if (!pickHit)
				return;
			int hx = pickHX, hy = pickHY, hz = pickHZ;
			bool mx = MirrorOn(0), my = MirrorOn(1), mz = MirrorOn(2);
			int xb = mx ? MirrorIdx(hx, mirror.plane.x) : hx; int nx = (mx && xb != hx) ? 2 : 1;
			int yb = my ? MirrorIdx(hy, mirror.plane.y) : hy; int ny = (my && yb != hy) ? 2 : 1;
			int zb = mz ? MirrorIdx(hz, mirror.plane.z) : hz; int nz = (mz && zb != hz) ? 2 : 1;

			int n = 0;
			for (int ia = 0; ia < nx; ia++) { int X = (ia == 0) ? hx : xb;
			for (int ib = 0; ib < ny; ib++) { int Y = (ib == 0) ? hy : yb;
			for (int ic = 0; ic < nz; ic++) { int Z = (ic == 0) ? hz : zb;
				if (InBounds(X, Y, Z) && model->IsSolid(X, Y, Z))
					n++;
			}}}
			if (n == 0)
				return;
			if (voxelCount - n < 1) {
				SetStatus(kLastVoxelMessage);
				return;
			}
			KV6UndoStack::Step step(undo, "Delete");
			for (int ia = 0; ia < nx; ia++) { int X = (ia == 0) ? hx : xb;
			for (int ib = 0; ib < ny; ib++) { int Y = (ib == 0) ? hy : yb;
			for (int ic = 0; ic < nz; ic++) { int Z = (ic == 0) ? hz : zb;
				if (InBounds(X, Y, Z) && model->IsSolid(X, Y, Z)) {
					WriteVoxel(X, Y, Z, false, 0);
					selection.erase(SelKey(X, Y, Z)); // drop stale selection entries
				}
			}}}
			TrimVolume();
			RebuildRenderModel();
		}

		// --- Colour -----------------------------------------------------------

		uint32_t KV6EditorView::PackRGB(float r, float g, float b) const {
			int ri = std::max(0, std::min(255, int(r * 255.0F + 0.5F)));
			int gi = std::max(0, std::min(255, int(g * 255.0F + 0.5F)));
			int bi = std::max(0, std::min(255, int(b * 255.0F + 0.5F)));
			return uint32_t((bi << 16) | (gi << 8) | ri); // 0x00BBGGRR
		}

		uint32_t KV6EditorView::HSV(float h, float s, float v) const {
			float i = std::floor(h * 6.0F);
			float f = h * 6.0F - i;
			float p = v * (1.0F - s);
			float q = v * (1.0F - f * s);
			float t = v * (1.0F - (1.0F - f) * s);
			int ii = int(i) % 6;
			if (ii < 0) ii += 6;
			if (ii == 0) return PackRGB(v, t, p);
			if (ii == 1) return PackRGB(q, v, p);
			if (ii == 2) return PackRGB(p, v, t);
			if (ii == 3) return PackRGB(p, q, v);
			if (ii == 4) return PackRGB(t, p, v);
			return PackRGB(v, p, q);
		}

		Vector4 KV6EditorView::ColorToVec(uint32_t c) const {
			return MakeVector4(float(c & 0xFF) / 255.0F, float((c >> 8) & 0xFF) / 255.0F,
			                   float((c >> 16) & 0xFF) / 255.0F, 1.0F);
		}

		bool KV6EditorView::SamplingArmed() const {
			return altHeld || ui->GetColorPicker()->GetEyedropperMode();
		}

		bool KV6EditorView::SampleColor() {
			DoPick();
			if (!pickHit)
				return false;
			const uint32_t sampled = model->GetColor(pickHX, pickHY, pickHZ) & 0xFFFFFF;
			ColorPicker& picker = *ui->GetColorPicker();
			picker.SetColor(sampled);
			picker.SetEyedropperMode(false); // a pick is one-shot
			// Set after the picker, whose report back has been through HSV and may
			// be a shade off: the brush takes the voxel's exact colour.
			currentColor = sampled;
			SetStatus("Picked colour");
			return true;
		}

		// --- Selection --------------------------------------------------------

		void KV6EditorView::AddSelect(int x, int y, int z) { selection.insert(SelKey(x, y, z)); }
		bool KV6EditorView::IsSelected(int x, int y, int z) const {
			return selection.find(SelKey(x, y, z)) != selection.end();
		}
		void KV6EditorView::ClearSelection() {
			DocumentCommand command(*this);
			KV6UndoStack::Step step(undo, "Clear Selection");
			selection.clear();
		}

		std::vector<IntVector3> KV6EditorView::LinkedColorRegion(int x, int y, int z) const {
			std::vector<IntVector3> region;
			if (!InBounds(x, y, z) || !model->IsSolid(x, y, z))
				return region;
			uint32_t target = model->GetColor(x, y, z) & 0xFFFFFF;
			std::set<int64_t> visited;
			std::vector<IntVector3> stack;
			stack.push_back(MakeIntVector3(x, y, z));
			while (!stack.empty()) {
				IntVector3 c = stack.back();
				stack.pop_back();
				int64_t key = SelKey(c.x, c.y, c.z);
				if (!visited.insert(key).second)
					continue;
				if (!InBounds(c.x, c.y, c.z) || !model->IsSolid(c.x, c.y, c.z))
					continue;
				if ((model->GetColor(c.x, c.y, c.z) & 0xFFFFFF) != target)
					continue;
				region.push_back(c);
				stack.push_back(MakeIntVector3(c.x + 1, c.y, c.z));
				stack.push_back(MakeIntVector3(c.x - 1, c.y, c.z));
				stack.push_back(MakeIntVector3(c.x, c.y + 1, c.z));
				stack.push_back(MakeIntVector3(c.x, c.y - 1, c.z));
				stack.push_back(MakeIntVector3(c.x, c.y, c.z + 1));
				stack.push_back(MakeIntVector3(c.x, c.y, c.z - 1));
			}
			return region;
		}

		void KV6EditorView::DrawSelection() {
			Vector4 col = MakeVector4(1.0F, 0.55F, 0.1F, 0.95F);
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				DrawCellOutline(x, y, z, col);
			}
		}
		void KV6EditorView::ShiftSelection(int ox, int oy, int oz) {
			if (ox == 0 && oy == 0 && oz == 0)
				return;
			std::set<int64_t> shifted;
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				shifted.insert(SelKey(x + ox, y + oy, z + oz));
			}
			selection.swap(shifted);
		}

		// --- Clipboard / paste -----------------------------------------------

		void KV6EditorView::CopySelection() {
			DocumentCommand command(*this);
			if (selection.empty()) {
				SetStatus("Nothing selected");
				return;
			}
			int minX = model->GetWidth(), minY = model->GetHeight(), minZ = model->GetDepth();
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				minX = std::min(minX, x); minY = std::min(minY, y); minZ = std::min(minZ, z);
			}
			clipboard.clear();
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				if (InBounds(x, y, z) && model->IsSolid(x, y, z))
					clipboard.push_back({MakeIntVector3(x - minX, y - minY, z - minZ),
					                     model->GetColor(x, y, z) & 0xFFFFFF});
			}
			SetStatus("Copied " + std::to_string(clipboard.size()) + " voxels");
		}

		bool KV6EditorView::CutSelection() {
			DocumentCommand command(*this);
			if (!CanEraseSelection())
				return false;
			CopySelection();
			SetStatus("Cut " + std::to_string(EraseSelection("Cut")) + " voxels");
			return true;
		}

		void KV6EditorView::DeleteSelection() {
			DocumentCommand command(*this);
			if (!CanEraseSelection())
				return;
			SetStatus("Deleted " + std::to_string(EraseSelection("Delete")) + " voxels");
		}

		int KV6EditorView::SelectedVoxelCount() const {
			int count = 0;
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				if (InBounds(x, y, z) && model->IsSolid(x, y, z))
					count++;
			}
			return count;
		}

		bool KV6EditorView::CanEraseSelection() {
			const int count = SelectedVoxelCount();
			if (count == 0) {
				SetStatus("Nothing selected");
				return false;
			}
			if (voxelCount - count < 1) {
				SetStatus(kLastVoxelMessage);
				return false;
			}
			return true;
		}

		int KV6EditorView::EraseSelection(const std::string& label) {
			KV6UndoStack::Step step(undo, label);
			int erased = 0;
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				if (InBounds(x, y, z) && model->IsSolid(x, y, z)) {
					WriteVoxel(x, y, z, false, 0);
					erased++;
				}
			}
			selection.clear();
			TrimVolume();
			RebuildRenderModel();
			return erased;
		}

		void KV6EditorView::Paste() {
			if (clipboard.empty()) {
				SetStatus("Clipboard is empty");
				return;
			}
			IntVector3 anchor = MakeIntVector3(model->GetWidth() / 2, model->GetHeight() / 2,
			                                   model->GetDepth() / 2);
			StartPlacement(clipboard, "Paste", anchor);
		}

		void KV6EditorView::StartPlacement(std::vector<ClipVoxel> voxels, const std::string& label,
		                                   const IntVector3& anchor) {
			if (voxels.empty())
				return;
			// Anything already pending belongs to the document before this starts.
			DocumentCommand command(*this);

			IntVector3 at = anchor;
			if (!ClampPlacementAnchor(voxels, at)) {
				SetStatus(label + ": too large for the maximum model size");
				return;
			}
			{
				KV6UndoStack::Step step(undo, label);
				placement = MakePlacement(std::move(voxels), at, std::vector<IntVector3>(), label);
				placementActive = true;
			}
			RebuildPlacementModel();

			// Positioning a placement is what the Transform tool is for, so go there.
			if (!ActivateTransformTool()) {
				SetStatus(label + ": could not open the Transform tool");
				return;
			}
			SetStatus(label + ": position it with the gizmo, then click away to place it");
		}

		bool KV6EditorView::ActivateTransformTool() {
			// Matched by type rather than by label, so neither renaming a button nor
			// another sub-tool taking the same label can send a placement elsewhere.
			for (size_t i = 0; i < tools.size(); i++) {
				EditorTool& tool = *tools[i].tool;
				for (int sub = 0; sub < tool.SubToolCount(); sub++) {
					if (!dynamic_cast<TransformSubTool*>(tool.SubTool(sub)))
						continue;
					SetMode(EditorMode::Edit);
					SetActiveTool(int(i));
					tool.SetSubTool(*this, sub);
					return true;
				}
			}
			return false;
		}

		void KV6EditorView::ImportModel(const std::string& path) {
			Handle<VoxelModel> imported(io->Load(path), false); // adopt (Load returns a ref)
			if (!imported) {
				SetStatus("Could not load " + path);
				return;
			}

			// Collect the solid voxels relative to their own min corner. Interior
			// voxels keep the sentinel colour the loader fills them with, exactly as
			// in a document opened from disk; the writer drops them again on save.
			int minX = imported->GetWidth(), minY = imported->GetHeight(),
			    minZ = imported->GetDepth();
			int maxX = -1, maxY = -1, maxZ = -1;
			for (int x = 0; x < imported->GetWidth(); x++)
			for (int y = 0; y < imported->GetHeight(); y++)
			for (int z = 0; z < imported->GetDepth(); z++) {
				if (!imported->IsSolid(x, y, z))
					continue;
				minX = std::min(minX, x); maxX = std::max(maxX, x);
				minY = std::min(minY, y); maxY = std::max(maxY, y);
				minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
			}
			if (maxX < 0) {
				SetStatus("That model is empty");
				return;
			}

			std::vector<ClipVoxel> voxels;
			for (int x = minX; x <= maxX; x++)
			for (int y = minY; y <= maxY; y++)
			for (int z = minZ; z <= maxZ; z++) {
				if (imported->IsSolid(x, y, z))
					voxels.push_back({MakeIntVector3(x - minX, y - minY, z - minZ),
					                  imported->GetColor(x, y, z) & 0xFFFFFF});
			}

			// Start with the imported pivot on this document's pivot, which puts a
			// part (a hand, a barrel) where it belongs before any dragging.
			Vector3 importedPivot = imported->GetOrigin() * -1.0F;
			Vector3 aligned =
			  MakeVector3(float(minX), float(minY), float(minZ)) + GetPivot() - importedPivot;
			IntVector3 anchor = MakeIntVector3(int(std::floor(aligned.x + 0.5F)),
			                                   int(std::floor(aligned.y + 0.5F)),
			                                   int(std::floor(aligned.z + 0.5F)));
			StartPlacement(std::move(voxels), "Import", anchor);
		}

		bool KV6EditorView::PlacementFromSelection(PendingPlacement& out) const {
			std::vector<IntVector3> lifted;
			IntVector3 lo = MakeIntVector3(INT_MAX, INT_MAX, INT_MAX);
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				if (!InBounds(x, y, z) || !model->IsSolid(x, y, z))
					continue;
				lifted.push_back(MakeIntVector3(x, y, z));
				lo.x = std::min(lo.x, x);
				lo.y = std::min(lo.y, y);
				lo.z = std::min(lo.z, z);
			}
			if (lifted.empty())
				return false;
			std::vector<ClipVoxel> voxels;
			voxels.reserve(lifted.size());
			for (const IntVector3& v : lifted)
				voxels.push_back({v - lo, model->GetColor(v.x, v.y, v.z) & 0xFFFFFF});
			out = MakePlacement(std::move(voxels), lo, std::move(lifted), "Transform");
			return true;
		}

		void KV6EditorView::LiftIntoPlacement(PendingPlacement taken) {
			// The voxels leave the document, journaled, so the gap they came from
			// shows while they are positioned and undo puts them back.
			for (const IntVector3& v : taken.lifted) {
				if (InBounds(v.x, v.y, v.z))
					WriteVoxel(v.x, v.y, v.z, false, 0);
			}
			selection.clear(); // the lifted voxels are what is selected now
			placement = std::move(taken);
			placementActive = true;
			RebuildRenderModel();
		}

		KV6EditorView::DocumentCommand::DocumentCommand(KV6EditorView& editor) : editor(editor) {
			if (editor.documentCommandDepth++ == 0) {
				editor.EndPreviews();
				editor.ApplyPlacement();
			}
		}

		KV6EditorView::DocumentCommand::~DocumentCommand() {
			// A command that threw has not finished, so there is nothing yet for
			// the tool to catch up with (and a destructor must not throw on top).
			if (--editor.documentCommandDepth == 0 && std::uncaught_exceptions() == 0)
				editor.NotifyDocumentChanged();
		}

		void KV6EditorView::RebuildPlacementModel() {
			placementModel = Handle<client::IModel>();
			if (!placementActive || placement.voxels.empty())
				return;
			const IntVector3 extent = ExtentOf(placement.voxels);
			Handle<VoxelModel> preview = Handle<VoxelModel>::New(extent.x, extent.y, extent.z);
			for (const ClipVoxel& v : placement.voxels)
				preview->SetSolid(v.rel.x, v.rel.y, v.rel.z, v.color);
			placementModel = renderer->CreateModel(*preview);
		}

		void KV6EditorView::DropPlacement() {
			placementActive = false;
			placement = PendingPlacement();
			placementModel = Handle<client::IModel>();
		}

		IntVector3 KV6EditorView::ExtentOf(const std::vector<ClipVoxel>& voxels) {
			IntVector3 extent = MakeIntVector3(1, 1, 1);
			for (const ClipVoxel& v : voxels) {
				extent.x = std::max(extent.x, v.rel.x + 1);
				extent.y = std::max(extent.y, v.rel.y + 1);
				extent.z = std::max(extent.z, v.rel.z + 1);
			}
			return extent;
		}

		PendingPlacement KV6EditorView::MakePlacement(std::vector<ClipVoxel> voxels,
		                                              const IntVector3& anchor,
		                                              std::vector<IntVector3> lifted,
		                                              const std::string& label) {
			PendingPlacement p;
			p.voxels = std::move(voxels);
			p.anchor = anchor;
			// The voxel at the middle of their box (the lower one where the middle
			// falls between two), so a turn keeps them about where they were.
			const IntVector3 extent = ExtentOf(p.voxels);
			p.pivot = anchor + MakeIntVector3((extent.x - 1) / 2, (extent.y - 1) / 2,
			                                  (extent.z - 1) / 2);
			p.lifted = std::move(lifted);
			p.label = label;
			return p;
		}

		bool KV6EditorView::TransformedPlacement(const PendingPlacement& from,
		                                         const PlacementTransform& t,
		                                         PendingPlacement& out, bool& clamped) const {
			out = from;
			clamped = false;

			if (t.Turns()) {
				// A quarter turn takes the axis after `t.axis` onto the one after
				// that, which is the right-handed sense about `t.axis`. Offsets from
				// a whole-voxel centre stay whole, so every voxel lands on a voxel.
				const int turns = ((t.quarterTurns % 4) + 4) % 4;
				const int u = (t.axis + 1) % 3, v = (t.axis + 2) % 3;
				const IntVector3 centre = TurnCentre(from);
				auto turned = [&](const IntVector3& p) {
					const IntVector3 d = p - centre;
					int c[3] = {d.x, d.y, d.z};
					for (int k = 0; k < turns; k++) {
						const int cu = c[u];
						c[u] = -c[v];
						c[v] = cu;
					}
					return centre + MakeIntVector3(c[0], c[1], c[2]);
				};
				std::vector<IntVector3> cells(out.voxels.size());
				IntVector3 lo = MakeIntVector3(INT_MAX, INT_MAX, INT_MAX);
				for (size_t i = 0; i < out.voxels.size(); i++) {
					cells[i] = turned(from.anchor + out.voxels[i].rel);
					lo.x = std::min(lo.x, cells[i].x);
					lo.y = std::min(lo.y, cells[i].y);
					lo.z = std::min(lo.z, cells[i].z);
				}
				out.anchor = lo;
				for (size_t i = 0; i < out.voxels.size(); i++)
					out.voxels[i].rel = cells[i] - lo;
				// Their middle turns with them, so it is still their middle
				// whichever centre they turned about.
				out.pivot = turned(from.pivot);
			}

			out.anchor = out.anchor + t.shift;
			out.pivot = out.pivot + t.shift;
			IntVector3 fitted = out.anchor;
			if (!ClampPlacementAnchor(out.voxels, fitted))
				return false;
			clamped = !(fitted == out.anchor);
			// The pivot stays with the voxels, so turning back still returns them.
			out.pivot = out.pivot + (fitted - out.anchor);
			out.anchor = fitted;
			return true;
		}

		void KV6EditorView::TransformPlacement(const PlacementTransform& t) {
			if (!t.IsValid() || t.IsIdentity())
				return;
			// Nothing pending yet: the move takes the selected voxels with it.
			PendingPlacement selected;
			const bool lifting = !placementActive;
			if (lifting && !PlacementFromSelection(selected))
				return;

			PendingPlacement next;
			bool clamped = false;
			if (!TransformedPlacement(lifting ? selected : placement, t, next, clamped)) {
				SetStatus(kMaxModelSizeMessage);
				return;
			}
			if (clamped)
				SetStatus(kMaxModelSizeMessage); // went as far as the limit allows

			{
				KV6UndoStack::Step step(undo, t.Turns() ? "Turn" : "Move");
				if (lifting)
					LiftIntoPlacement(std::move(selected));
				placement = std::move(next);
			}
			RebuildPlacementModel();
		}

		bool KV6EditorView::TransformPivot(IntVector3& out) const {
			if (placementActive) {
				out = TurnCentre(placement);
				return true;
			}
			PendingPlacement selected;
			if (!PlacementFromSelection(selected))
				return false;
			out = TurnCentre(selected);
			return true;
		}

		IntVector3 KV6EditorView::TurnCentre(const PendingPlacement& group) const {
			if (!turnsAboutModelPivot)
				return group.pivot;
			// The voxel nearest the pivot; on a tie the lower one, as for the
			// middle of a group.
			const Vector3 p = GetPivot();
			auto nearest = [](float c) { return int(std::ceil(c - 0.5F)); };
			return MakeIntVector3(nearest(p.x), nearest(p.y), nearest(p.z));
		}

		bool KV6EditorView::ClampPlacementAnchor(const std::vector<ClipVoxel>& voxels,
		                                         IntVector3& anchor) const {
			const IntVector3 box = ExtentOf(voxels);
			const int extent[3] = {box.x, box.y, box.z};
			const int size[3] = {model->GetWidth(), model->GetHeight(), model->GetDepth()};
			const int limit[3] = {kMaxModelWidth, kMaxModelHeight, kMaxModelDepth};
			int at[3] = {anchor.x, anchor.y, anchor.z};
			for (int a = 0; a < 3; a++) {
				if (extent[a] > limit[a] || size[a] > limit[a])
					return false;
				// Landing grows the volume to span min(0, at) .. max(size, at + extent),
				// which stays within the limit exactly for `at` in this range.
				at[a] = std::max(size[a] - limit[a], std::min(limit[a] - extent[a], at[a]));
			}
			anchor = MakeIntVector3(at[0], at[1], at[2]);
			return true;
		}

		void KV6EditorView::ApplyPlacement() {
			if (!placementActive)
				return;

			// Volume that must hold the document plus the placed voxels. Every
			// placement is kept where it fits (ClampPlacementAnchor), so this holds;
			// were it ever not to, putting the voxels back would lose nothing.
			int loX = 0, loY = 0, loZ = 0;
			int hiX = model->GetWidth(), hiY = model->GetHeight(), hiZ = model->GetDepth();
			for (const ClipVoxel& v : placement.voxels) {
				const IntVector3 at = placement.anchor + v.rel;
				loX = std::min(loX, at.x); hiX = std::max(hiX, at.x + 1);
				loY = std::min(loY, at.y); hiY = std::max(hiY, at.y + 1);
				loZ = std::min(loZ, at.z); hiZ = std::max(hiZ, at.z + 1);
			}
			int nw = hiX - loX, nh = hiY - loY, nd = hiZ - loZ;
			if (!FitsModelSize(nw, nh, nd)) {
				CancelPlacement();
				SetStatus(kMaxModelSizeMessage);
				return;
			}

			const PendingPlacement pending = placement;
			int placed = 0;
			{
				KV6UndoStack::Step step(undo, pending.label);
				// No longer pending before the volume changes, so the relabelling
				// below leaves `pending` in the frame it was measured in.
				DropPlacement();
				int ox = -loX, oy = -loY, oz = -loZ;
				if (ox != 0 || oy != 0 || oz != 0 || nw != model->GetWidth() ||
				    nh != model->GetHeight() || nd != model->GetDepth())
					RebuildVolume(nw, nh, nd, ox, oy, oz);
				selection.clear();
				for (const ClipVoxel& v : pending.voxels) {
					const IntVector3 at = pending.anchor + v.rel + MakeIntVector3(ox, oy, oz);
					if (!InBounds(at.x, at.y, at.z))
						continue;
					WriteVoxel(at.x, at.y, at.z, true, v.color);
					AddSelect(at.x, at.y, at.z); // select what was just placed
					placed++;
				}
				TrimVolume();
			}
			RebuildRenderModel();
			SetStatus(pending.label + ": placed " + std::to_string(placed) + " voxels");
		}

		void KV6EditorView::CancelPlacement() {
			if (!placementActive)
				return;
			const PendingPlacement pending = placement;
			{
				KV6UndoStack::Step step(undo, "Cancel " + pending.label);
				DropPlacement();
				// Lifted voxels go back where they came from, selected as they were;
				// `lifted` and `voxels` are parallel, so each keeps its colour.
				for (size_t i = 0; i < pending.lifted.size(); i++) {
					const IntVector3& v = pending.lifted[i];
					if (!InBounds(v.x, v.y, v.z))
						continue;
					WriteVoxel(v.x, v.y, v.z, true, pending.voxels[i].color);
					AddSelect(v.x, v.y, v.z);
				}
			}
			RebuildRenderModel();
			SetStatus(pending.label + " cancelled");
		}

		void KV6EditorView::DrawPlacementPreview() {
			// The voxels themselves are rendered solid (see RunFrame); outline the
			// volume so it reads as pending rather than part of the document.
			if (!placementActive || placement.voxels.empty())
				return;
			IntVector3 lo = placement.anchor, hi = placement.anchor;
			for (const ClipVoxel& v : placement.voxels) {
				lo.x = std::min(lo.x, placement.anchor.x + v.rel.x);
				lo.y = std::min(lo.y, placement.anchor.y + v.rel.y);
				lo.z = std::min(lo.z, placement.anchor.z + v.rel.z);
				hi.x = std::max(hi.x, placement.anchor.x + v.rel.x);
				hi.y = std::max(hi.y, placement.anchor.y + v.rel.y);
				hi.z = std::max(hi.z, placement.anchor.z + v.rel.z);
			}
			DrawBoxOutline(lo, hi, MakeVector4(0.4F, 1.0F, 0.5F, 0.9F));
		}

		void KV6EditorView::DrawPlacementTransformed(const PlacementTransform& t,
		                                             const Vector4& color) {
			if (!t.IsValid())
				return;
			PendingPlacement selected;
			if (!placementActive && !PlacementFromSelection(selected))
				return;
			PendingPlacement preview;
			bool clamped = false;
			if (!TransformedPlacement(placementActive ? placement : selected, t, preview, clamped))
				return;
			for (const ClipVoxel& v : preview.voxels) {
				const IntVector3 at = preview.anchor + v.rel;
				DrawCellOutline(at.x, at.y, at.z, color);
			}
		}

		// --- Layout / hit testing --------------------------------------------

		bool KV6EditorView::InRect(const Vector2& p, float x, float y, float w, float h) const {
			return OverlayInRect(p, x, y, w, h);
		}

		// --- Drawing primitives ----------------------------------------------

		void KV6EditorView::ColorNP(const Vector4& c) { OverlayColorNP(*renderer, c); }
		void KV6EditorView::FillRect(float x, float y, float w, float h) {
			OverlayFillRect(*renderer, x, y, w, h);
		}
		void KV6EditorView::StrokeRect(float x, float y, float w, float h, float t,
		                               const Vector4& c) {
			OverlayStrokeRect(*renderer, x, y, w, h, t, c);
		}
		void KV6EditorView::DrawLine2D(const Vector2& a, const Vector2& b, float w,
		                               const Vector4& col) {
			OverlayStrokeLine(*renderer, a, b, w, col);
		}

		void KV6EditorView::DrawCellOutline(int x, int y, int z, const Vector4& color) {
			Vector3 a = MakeVector3(float(x) - 0.5F, float(y) - 0.5F, float(z) - 0.5F);
			Vector3 b = MakeVector3(float(x) + 0.5F, float(y) + 0.5F, float(z) + 0.5F);
			BoxEdges(a, b, [&](const Vector3& p, const Vector3& q) { EmitLine(p, q, color); });
		}

		void KV6EditorView::DrawBoxOutline(const IntVector3& lo, const IntVector3& hi,
		                                   const Vector4& color) {
			Vector3 a = MakeVector3(float(lo.x) - 0.5F, float(lo.y) - 0.5F, float(lo.z) - 0.5F);
			Vector3 b = MakeVector3(float(hi.x) + 0.5F, float(hi.y) + 0.5F, float(hi.z) + 0.5F);
			BoxEdges(a, b, [&](const Vector3& p, const Vector3& q) { EmitLine(p, q, color); });
		}

		void KV6EditorView::DrawCellOutlineMirrored(int x, int y, int z, const Vector4& color) {
			std::vector<IntVector3> cells;
			cells.push_back(MakeIntVector3(x, y, z));
			ExpandMirrors(cells);
			for (const IntVector3& c : cells)
				DrawCellOutline(c.x, c.y, c.z, color);
		}

		void KV6EditorView::DrawBoxOutlineMirrored(const IntVector3& lo, const IntVector3& hi,
		                                           const Vector4& color) {
			for (int mx = 0; mx <= (MirrorOn(0) ? 1 : 0); mx++)
			for (int my = 0; my <= (MirrorOn(1) ? 1 : 0); my++)
			for (int mz = 0; mz <= (MirrorOn(2) ? 1 : 0); mz++) {
				IntVector3 a = lo, b = hi;
				if (mx) { int p = MirrorIdx(lo.x, mirror.plane.x), q = MirrorIdx(hi.x, mirror.plane.x);
				          a.x = std::min(p, q); b.x = std::max(p, q); }
				if (my) { int p = MirrorIdx(lo.y, mirror.plane.y), q = MirrorIdx(hi.y, mirror.plane.y);
				          a.y = std::min(p, q); b.y = std::max(p, q); }
				if (mz) { int p = MirrorIdx(lo.z, mirror.plane.z), q = MirrorIdx(hi.z, mirror.plane.z);
				          a.z = std::min(p, q); b.z = std::max(p, q); }
				DrawBoxOutline(a, b, color);
			}
		}

		void KV6EditorView::SelectBox(const IntVector3& lo, const IntVector3& hi) {
			DocumentCommand command(*this);
			KV6UndoStack::Step step(undo, "Select");
			for (int x = lo.x; x <= hi.x; x++)
			for (int y = lo.y; y <= hi.y; y++)
			for (int z = lo.z; z <= hi.z; z++) {
				if (InBounds(x, y, z) && model->IsSolid(x, y, z))
					AddSelect(x, y, z);
			}
		}

		void KV6EditorView::SelectCells(const std::vector<IntVector3>& cells) {
			DocumentCommand command(*this);
			KV6UndoStack::Step step(undo, "Select");
			for (const IntVector3& c : cells) {
				if (InBounds(c.x, c.y, c.z) && model->IsSolid(c.x, c.y, c.z))
					AddSelect(c.x, c.y, c.z);
			}
		}

		void KV6EditorView::FillCells(const std::vector<IntVector3>& cellsIn, uint32_t color) {
			DocumentCommand command(*this);
			std::vector<IntVector3> cells = cellsIn;
			ExpandMirrors(cells); // also fill the mirror images, if enabled
			if (cells.empty())
				return;
			// Grow the volume to contain every cell (plus the current model).
			int loX = 0, loY = 0, loZ = 0;
			int hiX = model->GetWidth(), hiY = model->GetHeight(), hiZ = model->GetDepth();
			for (const IntVector3& c : cells) {
				loX = std::min(loX, c.x); hiX = std::max(hiX, c.x + 1);
				loY = std::min(loY, c.y); hiY = std::max(hiY, c.y + 1);
				loZ = std::min(loZ, c.z); hiZ = std::max(hiZ, c.z + 1);
			}
			int nw = hiX - loX, nh = hiY - loY, nd = hiZ - loZ;
			if (!FitsModelSize(nw, nh, nd)) {
				SetStatus(kMaxModelSizeMessage);
				return;
			}
			int ox = -loX, oy = -loY, oz = -loZ;
			KV6UndoStack::Step step(undo, "Fill");
			if (ox != 0 || oy != 0 || oz != 0 || nw != model->GetWidth() ||
			    nh != model->GetHeight() || nd != model->GetDepth())
				RebuildVolume(nw, nh, nd, ox, oy, oz);
			bool any = false;
			for (const IntVector3& c : cells) {
				int X = c.x + ox, Y = c.y + oy, Z = c.z + oz;
				if (InBounds(X, Y, Z) && !model->IsSolid(X, Y, Z)) {
					WriteVoxel(X, Y, Z, true, color);
					any = true;
				}
			}
			if (any)
				RebuildRenderModel();
		}

		void KV6EditorView::EraseCells(const std::vector<IntVector3>& cellsIn) {
			DocumentCommand command(*this);
			std::vector<IntVector3> cells = cellsIn;
			ExpandMirrors(cells); // also erase the mirror images, if enabled
			int count = 0;
			for (const IntVector3& c : cells) {
				if (InBounds(c.x, c.y, c.z) && model->IsSolid(c.x, c.y, c.z))
					count++;
			}
			if (count == 0)
				return;
			if (voxelCount - count < 1) {
				SetStatus(kLastVoxelMessage);
				return;
			}
			KV6UndoStack::Step step(undo, "Erase");
			for (const IntVector3& c : cells) {
				if (InBounds(c.x, c.y, c.z) && model->IsSolid(c.x, c.y, c.z)) {
					WriteVoxel(c.x, c.y, c.z, false, 0);
					selection.erase(SelKey(c.x, c.y, c.z));
				}
			}
			TrimVolume();
			RebuildRenderModel();
		}

		void KV6EditorView::DeselectCells(const std::vector<IntVector3>& cells) {
			DocumentCommand command(*this);
			KV6UndoStack::Step step(undo, "Deselect");
			for (const IntVector3& c : cells)
				selection.erase(SelKey(c.x, c.y, c.z));
		}

		void KV6EditorView::PaintCells(const std::vector<IntVector3>& cellsIn, uint32_t color) {
			DocumentCommand command(*this);
			std::vector<IntVector3> cells = cellsIn;
			ExpandMirrors(cells); // also recolour the mirror images, if enabled
			uint32_t rgb = color & 0xFFFFFF;
			KV6UndoStack::Step step(undo, "Paint");
			bool any = false;
			for (const IntVector3& c : cells) {
				if (!InBounds(c.x, c.y, c.z) || !model->IsSolid(c.x, c.y, c.z))
					continue; // paint only existing voxels; never grows the volume
				if ((model->GetColor(c.x, c.y, c.z) & 0xFFFFFF) == rgb)
					continue; // already this colour
				WriteVoxel(c.x, c.y, c.z, true, rgb);
				any = true;
			}
			if (any)
				RebuildRenderModel();
		}

		void KV6EditorView::ApplyCells(const std::vector<IntVector3>& cells, bool secondary) {
			EditorTool* t = ActiveTool();
			EditorRole role = t ? t->Role() : EditorRole::Edit;
			if (role == EditorRole::Select) {
				if (secondary) DeselectCells(cells);
				else SelectCells(cells);
			} else if (role == EditorRole::Paint) {
				PaintCells(cells, currentColor); // no inverse: both buttons recolour
			} else {
				if (secondary) EraseCells(cells);
				else FillCells(cells, currentColor);
			}
		}

		// --- Undo / redo ------------------------------------------------------

		// The one place voxels are written: keeps `voxelCount` correct and (unlike
		// WriteVoxelRaw) journals the change so it can be undone.
		void KV6EditorView::WriteVoxel(int x, int y, int z, bool solid, uint32_t color) {
			if (!InBounds(x, y, z))
				return;
			bool oldSolid = model->IsSolid(x, y, z);
			uint32_t oldColor = model->GetColor(x, y, z) & 0xFFFFFF;
			uint32_t newColor = solid ? (color & 0xFFFFFF) : oldColor; // air keeps its colour
			WriteVoxelRaw(x, y, z, solid, newColor);
			undo.RecordVoxel(x, y, z, oldSolid, oldColor, solid, newColor);
		}

		void KV6EditorView::WriteVoxelRaw(int x, int y, int z, bool solid, uint32_t color) {
			bool was = model->IsSolid(x, y, z);
			if (solid)
				model->SetSolid(x, y, z, color);
			else
				model->SetAir(x, y, z);
			if (was && !solid)
				voxelCount--;
			else if (!was && solid)
				voxelCount++;
		}

		EditState KV6EditorView::UndoSnapshotState() const {
			EditState state;
			state.selection = selection;
			state.mirror = mirror;
			state.placing = placementActive;
			if (placementActive)
				state.placement = placement;
			return state;
		}

		void KV6EditorView::UndoRestoreState(const EditState& state) {
			selection = state.selection;
			mirror = state.mirror;
			placementActive = state.placing;
			placement = state.placing ? state.placement : PendingPlacement();
		}

		// KV6UndoStack::Sink — the stack replays records through these.
		void KV6EditorView::UndoApplyVoxel(int x, int y, int z, bool solid, uint32_t color) {
			WriteVoxelRaw(x, y, z, solid, color);
		}
		void KV6EditorView::UndoApplyReframe(int w, int h, int d, int ox, int oy, int oz) {
			ReframeRaw(w, h, d, ox, oy, oz);
		}

		// Undo and redo replay the one history, pending voxels included: a
		// move or a turn is a step like any edit, so nothing is placed first.
		void KV6EditorView::Undo() {
			EndPreviews(); // the history holds only what was committed
			std::string label = undo.UndoLabel();
			if (!undo.Undo()) {
				SetStatus("Nothing to undo");
				return;
			}
			SetStatus("Undid " + (label.empty() ? std::string("edit") : label));
			HistoryReplayed();
		}
		void KV6EditorView::Redo() {
			EndPreviews(); // the history holds only what was committed
			std::string label = undo.RedoLabel();
			if (!undo.Redo()) {
				SetStatus("Nothing to redo");
				return;
			}
			SetStatus("Redid " + (label.empty() ? std::string("edit") : label));
			HistoryReplayed();
		}

		void KV6EditorView::HistoryReplayed() {
			if (placementActive)
				ActivateTransformTool();
			NotifyDocumentChanged();
		}

		// --- Pivot ------------------------------------------------------------

		Vector3 KV6EditorView::GetPivot() const { return model->GetOrigin() * -1.0F; }

		// The renderer bakes `origin` into the render model, so re-bake after changing
		// it; that keeps the voxels visually fixed while the pivot marker moves.
		void KV6EditorView::ApplyOriginRaw(const Vector3& origin) {
			model->SetOrigin(origin);
			RebuildRenderModel();
		}

		void KV6EditorView::SetPivot(const Vector3& pivot) {
			DocumentCommand command(*this);
			Vector3 before = model->GetOrigin();
			Vector3 after = pivot * -1.0F;
			if (after.x == before.x && after.y == before.y && after.z == before.z)
				return;
			KV6UndoStack::Step step(undo, "Set Pivot");
			ApplyOriginRaw(after);
			undo.RecordOrigin(before, after);
		}

		void KV6EditorView::UndoApplyOrigin(const Vector3& origin) { ApplyOriginRaw(origin); }

		// Live, non-journaled pivot move for a drag in progress; the tool commits the
		// net change with one SetPivot on release.
		void KV6EditorView::PreviewPivot(const Vector3& pivot) {
			if (!previewedOrigin)
				previewedOrigin = model->GetOrigin();
			ApplyOriginRaw(pivot * -1.0F);
		}

		void KV6EditorView::BeginPivotEntry() {
			Vector3 p = GetPivot();
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%.1f %.1f %.1f", p.x, p.y, p.z);
			ui->GetEditorMenu()->OpenTextPrompt("Set pivot (x y z)", buf, [this](const std::string& s) {
				std::string str = s;
				for (char& c : str)
					if (c == ',')
						c = ' ';
				float x, y, z;
				if (std::sscanf(str.c_str(), "%f %f %f", &x, &y, &z) == 3) {
					SetPivot(MakeVector3(x, y, z));
					SetStatus("Pivot set");
				} else {
					SetStatus("Enter three numbers: x y z");
				}
			});
		}

		// Long world axes through the model's origin (pivot), which renders at
		// world coordinate -origin in the editor's grid space.
		void KV6EditorView::DrawOriginAxes() {
			Vector3 org = model->GetOrigin();
			Vector3 p = MakeVector3(-org.x, -org.y, -org.z);
			float L = 1000.0F;
			renderer->AddDebugLine(p - MakeVector3(L, 0, 0), p + MakeVector3(L, 0, 0),
			                       MakeVector4(1.0F, 0.3F, 0.3F, 0.5F));
			renderer->AddDebugLine(p - MakeVector3(0, L, 0), p + MakeVector3(0, L, 0),
			                       MakeVector4(0.4F, 1.0F, 0.4F, 0.5F));
			renderer->AddDebugLine(p - MakeVector3(0, 0, L), p + MakeVector3(0, 0, L),
			                       MakeVector4(0.45F, 0.6F, 1.0F, 0.5F));
		}

		// The ribbon + main toolbar + sub-toolbar are always present; the 3D
		// viewport sits below them.
		float KV6EditorView::BarsH() { return kBarsH; }

		void KV6EditorView::DrawHelpers() {
			// Opaque (alpha 1.0, so the lines cover what is behind them) but kept
			// close to the background tone: the floor should be legible without
			// competing with the model. Contrast comes from the minor/major
			// difference rather than from brightness.
			Vector4 grid = MakeVector4(0.19F, 0.20F, 0.23F, 1.0F);
			Vector4 gridMajor = MakeVector4(0.30F, 0.32F, 0.37F, 1.0F);
			Vector4 box = MakeVector4(0.4F, 0.7F, 1.0F, 0.5F);

			// A fixed grid in world space: it does not follow the model, the volume
			// or the camera, so it reads as the ground rather than something moving
			// with the subject.
			float z = kGroundPlaneZ;
			float from = float(-kGroundReach) - 0.5F; // voxel edges, not centres
			float to = float(kGroundReach) - 0.5F;
			for (int i = -kGroundReach; i <= kGroundReach; i += kGroundStep) {
				float at = float(i) - 0.5F;
				bool major = (i % (kGroundStep * kGroundMajorEvery)) == 0;
				const Vector4& color = major ? gridMajor : grid;
				renderer->AddDebugLine(MakeVector3(at, from, z), MakeVector3(at, to, z), color);
				renderer->AddDebugLine(MakeVector3(from, at, z), MakeVector3(to, at, z), color);
			}

			float lo = -0.5F;
			Vector3 a = MakeVector3(lo, lo, lo);
			Vector3 b = MakeVector3(float(model->GetWidth()) - 0.5F,
			                        float(model->GetHeight()) - 0.5F,
			                        float(model->GetDepth()) - 0.5F);
			BoxEdges(a, b, [&](const Vector3& p, const Vector3& q) {
				renderer->AddDebugLine(p, q, box);
			});
		}

		// Semi-transparent quad at each enabled mirror plane (drawn as a fan of
		// debug lines, since the editor only has line primitives in 3D).
		void KV6EditorView::DrawMirrorPlanes() {
			int w = model->GetWidth(), h = model->GetHeight(), d = model->GetDepth();

			// Grid lines are spaced one tile (2 voxels) apart. Extend one tile past
			// the model on every side, rounding the far edge up to the tile grid so
			// the plane is a whole number of tiles and always shows a complete border
			// (the model spans [-0.5, dim-0.5]; the near edge -2.5 is already aligned).
			auto stop = [](int n) { return (n % 2 == 0) ? n + 2 : n + 3; };
			float lo = -2.5F;
			float hiX = float(stop(w)) - 0.5F;
			float hiY = float(stop(h)) - 0.5F;
			float hiZ = float(stop(d)) - 0.5F;

			if (MirrorOn(0)) {
				float px = mirror.plane.x;
				Vector4 col = MakeVector4(1.0F, 0.35F, 0.35F, 0.25F);
				for (int i = -2; i <= stop(h); i += 2)
					renderer->AddDebugLine(MakeVector3(px, float(i) - 0.5F, lo),
					                       MakeVector3(px, float(i) - 0.5F, hiZ), col);
				for (int i = -2; i <= stop(d); i += 2)
					renderer->AddDebugLine(MakeVector3(px, lo, float(i) - 0.5F),
					                       MakeVector3(px, hiY, float(i) - 0.5F), col);
			}
			if (MirrorOn(1)) {
				float py = mirror.plane.y;
				Vector4 col = MakeVector4(0.4F, 1.0F, 0.4F, 0.25F);
				for (int i = -2; i <= stop(w); i += 2)
					renderer->AddDebugLine(MakeVector3(float(i) - 0.5F, py, lo),
					                       MakeVector3(float(i) - 0.5F, py, hiZ), col);
				for (int i = -2; i <= stop(d); i += 2)
					renderer->AddDebugLine(MakeVector3(lo, py, float(i) - 0.5F),
					                       MakeVector3(hiX, py, float(i) - 0.5F), col);
			}
			if (MirrorOn(2)) {
				float pz = mirror.plane.z;
				Vector4 col = MakeVector4(0.45F, 0.6F, 1.0F, 0.25F);
				for (int i = -2; i <= stop(w); i += 2)
					renderer->AddDebugLine(MakeVector3(float(i) - 0.5F, lo, pz),
					                       MakeVector3(float(i) - 0.5F, hiY, pz), col);
				for (int i = -2; i <= stop(h); i += 2)
					renderer->AddDebugLine(MakeVector3(lo, float(i) - 0.5F, pz),
					                       MakeVector3(hiX, float(i) - 0.5F, pz), col);
			}
		}

		// --- Navigation cube -------------------------------------------------

		void KV6EditorView::FillTri(const Vector2& A, const Vector2& B, const Vector2& C,
		                            const Vector4& col) {
			// Hard-edged on purpose: triangles tiling a larger shape would show
			// seams where two anti-aliased edges meet. Use OverlayFillConvexPolygon
			// for a standalone shape.
			ColorNP(col);
			renderer->DrawFilledTriangle(A, B, C);
		}

		GizmoView KV6EditorView::GetGizmoView() const { return camera; }

		void KV6EditorView::DrawGizmo(const TransformGizmo& gizmo) {
			GizmoCanvas canvas(*renderer);
			gizmo.Draw(canvas, GetGizmoView());
		}

		// A solid, shaded cube drawn as a 2D overlay: project the camera-facing faces
		// and fill them (offered to tool scripts for solid markers).
		void KV6EditorView::DrawSolidCube(const Vector3& center, float half,
		                                  const Vector4& color) {
			Vector3 corner[8];
			for (int i = 0; i < 8; i++)
				corner[i] = center + MakeVector3((i & 1) ? half : -half, (i & 2) ? half : -half,
				                                 (i & 4) ? half : -half);
			static const int faceIdx[6][4] = {{0, 2, 6, 4}, {1, 5, 7, 3}, {0, 4, 5, 1},
			                                  {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 6, 7, 5}};
			static const float faceN[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
			                                  {0, 1, 0},  {0, 0, -1}, {0, 0, 1}};
			for (int f = 0; f < 6; f++) {
				Vector3 n = MakeVector3(faceN[f][0], faceN[f][1], faceN[f][2]);
				Vector3 toCam = camera.eye - (center + n * half);
				float facing = Vector3::Dot(n, toCam);
				if (facing <= 0.0F)
					continue; // back-facing
				Vector2 q[4];
				bool ok = true, o;
				for (int i = 0; i < 4; i++) {
					q[i] = WorldToScreen(corner[faceIdx[f][i]], o);
					ok = ok && o;
				}
				if (!ok)
					continue;
				float sh = 0.55F + 0.45F * (facing / std::max(toCam.GetLength(), 1.0e-4F));
				Vector4 col = MakeVector4(color.x * sh, color.y * sh, color.z * sh, color.w);
				FillTri(q[0], q[1], q[2], col);
				FillTri(q[0], q[2], q[3], col);
			}
		}

		bool KV6EditorView::NaviCubeDir(const Vector2& p, Vector3& dir) {
			const std::vector<NaviFacet>& facets = NaviFacets();
			for (const NaviFacet& f : facets) {
				if (-Vector3::Dot(f.dir, camera.forward) <= 0.02F)
					continue; // back-facing
				Vector2 q[4];
				for (int i = 0; i < f.n; i++)
					q[i] = MakeVector2(gizCx + Vector3::Dot(f.v[i], camera.right) * gizR,
					                   gizCy - Vector3::Dot(f.v[i], camera.up) * gizR);
				if (PointInPoly(p, q, f.n)) { dir = f.dir; return true; }
			}
			return false;
		}

		void KV6EditorView::DrawNaviCube() {
			client::IFont& font = fontManager->GetSmallGuiFont();
			const std::vector<NaviFacet>& facets = NaviFacets();
			Vector3 hdir;
			bool hov = NaviCubeDir(softwareCursor->GetPosition(), hdir);
			// Draw bevels behind the faces: corners, then edges, then faces.
			for (int pass = 0; pass < 3; pass++) {
				for (const NaviFacet& f : facets) {
					bool isFace = f.tint >= 0;
					bool isCorner = (f.n == 3);
					bool isEdge = !isFace && !isCorner;
					if ((pass == 0 && !isCorner) || (pass == 1 && !isEdge) || (pass == 2 && !isFace))
						continue;
					float facing = -Vector3::Dot(f.dir, camera.forward);
					if (facing <= 0.02F)
						continue;
					Vector2 q[4];
					for (int i = 0; i < f.n; i++)
						q[i] = MakeVector2(gizCx + Vector3::Dot(f.v[i], camera.right) * gizR,
						                   gizCy - Vector3::Dot(f.v[i], camera.up) * gizR);
					float sh = 0.45F + 0.55F * facing;
					Vector4 base = isFace ? AxisTint(f.tint) : MakeVector4(0.5F, 0.52F, 0.56F, 1.0F);
					bool hl = hov && Vector3::Dot(hdir, f.dir) > 0.999F;
					Vector4 col = hl ? MakeVector4(0.4F, 0.7F, 1.0F, 0.97F)
					                 : MakeVector4(base.x * sh, base.y * sh, base.z * sh, 0.97F);
					// Each facet is one convex polygon so its whole outline is
					// anti-aliased; the seams it leaves against its neighbours are
					// covered by the edge lines drawn next.
					OverlayFillConvexPolygon(*renderer, q, std::size_t(f.n), col);
					Vector4 ec = MakeVector4(0.08F, 0.08F, 0.1F, 0.85F);
					for (int e = 0; e < f.n; e++)
						DrawLine2D(q[e], q[(e + 1) % f.n], 1.0F, ec);
				}
			}
			// Face labels last, so they sit on top of the cube.
			for (const NaviFacet& f : facets) {
				if (!f.label || -Vector3::Dot(f.dir, camera.forward) <= 0.02F)
					continue;
				Vector2 ctr = MakeVector2(0.0F, 0.0F);
				for (int i = 0; i < 4; i++)
					ctr += MakeVector2(gizCx + Vector3::Dot(f.v[i], camera.right) * gizR,
					                   gizCy - Vector3::Dot(f.v[i], camera.up) * gizR);
				ctr = ctr * 0.25F;
				float ls = 0.85F;
				Vector2 ts = font.Measure(f.label);
				font.DrawShadow(f.label, ctr - MakeVector2(ts.x * ls * 0.5F, ts.y * ls * 0.5F), ls,
				                MakeVector4(1, 1, 1, 1), MakeVector4(0, 0, 0, 0.8F));
			}
		}

		void KV6EditorView::SnapCameraDir(const Vector3& dir) {
			Vector3 f = dir * -1.0F; // camera forward = look toward the model
			float tp, ty;
			if (std::fabs(f.z) > 0.999F) {
				tp = asinf(std::max(-1.0F, std::min(1.0F, -f.z)));
				ty = yaw; // looking straight up/down: keep heading
			} else {
				tp = asinf(-f.z);
				ty = atan2f(f.y, f.x);
			}
			// Shortest angular path for yaw.
			while (ty - yaw > M_PI_F) ty -= 2.0F * M_PI_F;
			while (ty - yaw < -M_PI_F) ty += 2.0F * M_PI_F;
			targetYaw = ty;
			targetPitch = tp;
			camAnim = true;
		}

		std::string KV6EditorView::ToolHintLine() {
			if (SamplingArmed()) {
				std::string hint = "Pick colour:  click a voxel";
				if (ui->GetColorPicker()->GetEyedropperMode())
					hint += kHintSeparator + "[Esc] cancel";
				return hint;
			}
			EditorTool* tool = ActiveTool();
			if (!tool)
				return std::string();
			// A tool whose only sub-tool is itself goes by its own name alone.
			std::string name = tool->Label();
			if (tool->SubToolCount() > 1)
				name += std::string(" \xE2\x80\xBA ") + tool->SubToolLabel(tool->ActiveSubTool());
			const std::string hint = tool->Hint(*this);
			return hint.empty() ? name : name + ":  " + hint;
		}

		void KV6EditorView::DrawOverlay(float sw, float sh) {
			client::IFont& font = fontManager->GetSmallGuiFont();
			const float lineH = 22.0F;
			const float left = 16.0F;

			// Title / filename live in the ribbon; the tools' own keys are on
			// their buttons and in the tool hint, so this line keeps to the keys
			// that work everywhere.
			std::string help = "[MMB] look  |  [Shift+MMB] pan  |  [WASD/Space/Ctrl] move "
			                   "(+Shift faster)  |  [Wheel] zoom  |  [Alt+LMB] pick colour  |  "
			                   "[Ctrl+Z/Y] undo/redo  |  [Ctrl+C/X/V] copy/cut/paste";
			const std::string deleteKey = cg_keyDelete;
			if (!deleteKey.empty())
				help += kHintSeparator + "[" + deleteKey + "] delete selection";
			help += kHintSeparator + "[Esc] cancel / menu";

			// Text stays clear of the colour picker when it is open.
			float right = sw - left;
			ColorPicker& picker = *ui->GetColorPicker();
			if (picker.IsOpen())
				right = std::min(right, picker.GetBounds().GetMinX() - left);
			const float width = std::max(0.0F, right - left);

			// Bottom up: the help, the tool hint above it, the status above that.
			float y = sh - 28.0F;
			auto drawUp = [&](const std::string& text, const Vector4& color) {
				const std::vector<std::string> lines = WrapParts(font, text, width);
				for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
					font.Draw(*it, MakeVector2(left, y), 1.0F, color);
					y -= lineH;
				}
			};
			drawUp(help, MakeVector4(0.6F, 0.6F, 0.63F, 1.0F));
			drawUp(ToolHintLine(), MakeVector4(0.9F, 0.9F, 0.93F, 1.0F));
			if (statusTimer > 0.0F)
				drawUp(statusMessage, MakeVector4(0.5F, 1.0F, 0.6F, 1.0F));
		}

		void KV6EditorView::SetActiveTool(int index) {
			if (index < 0 || index >= int(tools.size()) || index == activeTool)
				return;
			// Leaving a tool ends what it had in progress: this is where a pending
			// placement reaches the document.
			if (EditorTool* previous = ActiveTool())
				previous->OnDeactivate(*this);
			activeTool = index;
			if (EditorTool* current = ActiveTool())
				current->OnActivate(*this);
		}

		std::string KV6EditorView::SubToolHotKey(int index) {
			return (index >= 0 && index < 9) ? std::to_string(index + 1) : std::string();
		}

		bool KV6EditorView::KeyIsTaken(const std::string& key) const {
			const std::string bound[] = {cg_keyMoveForward, cg_keyMoveBackward, cg_keyMoveLeft,
			                             cg_keyMoveRight,   cg_keyJump,         cg_keyCrouch,
			                             cg_keySprint,      cg_keyDelete,       cg_keyScreenshot};
			for (const std::string& b : bound) {
				if (KV6CheckKey(b, key))
					return true;
			}
			return false;
		}

		bool KV6EditorView::SwitchByHotKey(const std::string& key) {
			// Never mid-press: the tool taking over would see a drag it never saw start.
			if (key.empty() || lmbHeld || rmbHeld || KeyIsTaken(key))
				return false;
			for (size_t i = 0; i < tools.size(); i++) {
				if (!tools[i].hotKey.empty() && EqualsIgnoringCase(tools[i].hotKey, key)) {
					SetActiveTool(int(i));
					return true;
				}
			}
			EditorTool* tool = ActiveTool();
			if (!tool || tool->SubToolCount() < 2)
				return false;
			for (int sub = 0; sub < tool->SubToolCount(); sub++) {
				if (SubToolHotKey(sub) == key) {
					tool->SetSubTool(*this, sub);
					return true;
				}
			}
			return false;
		}

		void KV6EditorView::SetMode(EditorMode mode) {
			if (mode == currentMode)
				return;
			if (EditorTool* previous = ActiveTool())
				previous->OnDeactivate(*this);
			currentMode = mode;
			if (EditorTool* current = ActiveTool())
				current->OnActivate(*this);
		}

		EditorTool* KV6EditorView::ActiveTool() {
			if (currentMode == EditorMode::Edit && activeTool >= 0 && activeTool < int(tools.size()))
				return tools[activeTool].tool.get();
			return nullptr;
		}

		PointerInput KV6EditorView::MakePointer(PointerButton b, PointerPhase ph,
		                                        const Vector2& delta) const {
			PointerInput e;
			e.button = b;
			e.phase = ph;
			e.pos = softwareCursor->GetPosition();
			e.delta = delta;
			e.alt = altHeld;
			e.ctrl = ctrlHeld;
			e.shift = shiftHeld;
			return e;
		}

		void KV6EditorView::DispatchPointer(const PointerInput& e) {
			// A press starts one user action and the release of the last held
			// button ends it, so a stroke or a drag undoes as one step. (The held
			// flags already include this press and exclude this release.)
			if (e.IsDown() && !(lmbHeld && rmbHeld))
				undo.BeginAction();
			UserActionEnd end(*this, e.IsUp() && !lmbHeld && !rmbHeld);
			EditorTool* t = ActiveTool();
			if (!t)
				return;
			// The right button is the inverse of the left, and recolouring has
			// none: the button does nothing in Paint, whatever the sub-tool.
			if (e.IsRight() && t->Role() == EditorRole::Paint)
				return;
			t->OnPointer(*this, e);
		}

		void KV6EditorView::CancelToolInteraction() {
			if (EditorTool* t = ActiveTool())
				t->CancelInteraction(*this);
			EndUserAction(); // whatever comes next is a separate step
		}

		void KV6EditorView::EndUserAction() {
			EndPreviews();
			undo.EndAction();
		}

		void KV6EditorView::EndPreviews() {
			if (previewedOrigin) {
				ApplyOriginRaw(*previewedOrigin);
				previewedOrigin.reset();
			}
			if (previewedMirrorPlane) {
				mirror.plane = *previewedMirrorPlane;
				previewedMirrorPlane.reset();
			}
		}

		void KV6EditorView::NotifyDocumentChanged() {
			if (EditorTool* t = ActiveTool())
				t->OnDocumentChanged(*this);
		}

		// --- Ribbon (title) + unified toolbar [modes] | [tools] -------------
		//
		// Two stacked full-width bars at the very top: a ribbon (title/filename)
		// above a left-aligned toolbar. The 3D viewport is drawn below them.
		void KV6EditorView::DrawToolbar(float sw, float sh) {
			screenWidth = sw; // cache for hit detection in MouseEvent
			(void)sh;
			if (!ui) return;

			// Mode buttons, one per EditorMode in order; the ones this document
			// does not support are greyed out.
			std::vector<Toolbar::ToolbarButton> modeButtons;
			for (EditorMode mode : {EditorMode::Object, EditorMode::Edit, EditorMode::Animation}) {
				Toolbar::ToolbarButton btn;
				btn.label = mode == EditorMode::Object ? "Object"
				            : mode == EditorMode::Edit ? "Edit"
				                                       : "Animation";
				btn.enabled = ModeSupported(mode);
				btn.active = mode == currentMode;
				modeButtons.push_back(btn);
			}
			ui->GetToolbar()->SetModeButtons(modeButtons);

			// Tool buttons. A hot key the editor already uses for something else
			// never reaches the tool, so the button does not claim it.
			int toolCount = (currentMode == EditorMode::Edit) ? int(tools.size()) : 0;
			std::vector<Toolbar::ToolbarButton> toolButtons;
			for (int i = 0; i < toolCount; i++) {
				Toolbar::ToolbarButton btn;
				btn.label = tools[i].tool->Label();
				btn.hotKey = KeyIsTaken(tools[i].hotKey) ? std::string() : tools[i].hotKey;
				btn.active = (activeTool == i);
				btn.group = tools[i].group;
				toolButtons.push_back(btn);
			}
			ui->GetToolbar()->SetToolButtons(toolButtons);

			// Set undo/redo state
			ui->GetToolbar()->SetUndoButton(undo.CanUndo());
			ui->GetToolbar()->SetRedoButton(undo.CanRedo());

			// Draw the toolbar
			ui->GetToolbar()->Draw(*renderer, *fontManager, softwareCursor->GetPosition(),
					ui->GetEditorMenu()->IsActive(), sw);
		}

		bool KV6EditorView::MirrorOn(int axis) const { return MirrorEnabled(axis); }

		bool KV6EditorView::MirrorEnabled(int axis) const {
			if (axis < 0 || axis > 2)
				return false;
			// Mirroring is a modelling aid for Edit mode; it must not silently
			// reshape anything while another mode is driving the document.
			if (currentMode != EditorMode::Edit)
				return false;
			return mirror.enabled[axis];
		}

		// The mirror setup is journaled like the selection (each undo step
		// restores it), so changing it is a command like any edit.
		void KV6EditorView::SetMirrorEnabled(int axis, bool on) {
			if (axis < 0 || axis > 2)
				return;
			DocumentCommand command(*this);
			KV6UndoStack::Step step(undo, "Mirror Axis");
			mirror.enabled[axis] = on;
		}

		void KV6EditorView::SetMirrorPlane(const Vector3& plane) {
			DocumentCommand command(*this);
			KV6UndoStack::Step step(undo, "Move Mirror");
			PlaceMirrorPlane(plane);
		}

		void KV6EditorView::PreviewMirrorPlane(const Vector3& plane) {
			if (!previewedMirrorPlane)
				previewedMirrorPlane = mirror.plane;
			PlaceMirrorPlane(plane);
		}

		void KV6EditorView::ResetMirrorPlane() {
			DocumentCommand command(*this);
			KV6UndoStack::Step step(undo, "Reset Mirror");
			PlaceMirrorPlane(GetPivot());
		}

		void KV6EditorView::PlaceMirrorPlane(const Vector3& plane) {
			// MirrorIdx only sees whole half steps, so anything finer would move the
			// handle without moving the reflection.
			auto snap = [](float v) { return float(HalfSteps(v)) * 0.5F; };
			mirror.plane = MakeVector3(snap(plane.x), snap(plane.y), snap(plane.z));
		}

		void KV6EditorView::DrawSubToolbar(float sw) {
			if (!ui)
				return;
			// With no tool active the bar is drawn empty, so it never keeps
			// answering clicks on buttons that are no longer there.
			EditorTool* t = ActiveTool();

			// Sub-tool buttons for the option bar. A single sub-tool is the tool
			// itself, and a lone button that is always on would only be noise.
			std::vector<OptionBar::SubToolButton> subTools;
			if (t && t->SubToolCount() > 1) {
				for (int i = 0; i < t->SubToolCount(); i++) {
					OptionBar::SubToolButton btn;
					btn.label = t->SubToolLabel(i);
					btn.hotKey = KeyIsTaken(SubToolHotKey(i)) ? std::string() : SubToolHotKey(i);
					btn.active = (t->ActiveSubTool() == i);
					subTools.push_back(btn);
				}
			}
			ui->GetOptionBar()->SetSubToolButtons(subTools);

			// Build options for the tool
			std::vector<OptionBar::Option> options;
			if (t)
				t->UpdateOptions(*this);
			ToolOptions* opts = t ? t->Options() : nullptr;
			if (opts) {
				// The brush swatch is a view of the editor's single current colour,
				// not a per-tool value: the picker, the eyedropper and every tool's
				// swatch must always agree.
				for (int i = 0; i < opts->Count(); i++) {
					if (opts->At(i).id == kBrushColorOption)
						opts->At(i).color = currentColor;
				}
				for (int i = 0; i < opts->Count(); i++) {
					const ToolOption& op = opts->At(i);
					OptionBar::Option opt;
					opt.group = op.group;
					opt.label = op.label;
					opt.type = (op.type == ToolOption::Type::Color)  ? OptionBar::OptionType::Color
							 : (op.type == ToolOption::Type::Label)  ? OptionBar::OptionType::Label
							 : (op.type == ToolOption::Type::Action) ? OptionBar::OptionType::Action
							 : OptionBar::OptionType::Bool;
					opt.bvalue = op.bvalue;
					opt.enabled = op.enabled;
					opt.color = op.color;
					options.push_back(opt);
				}
			}
			ui->GetOptionBar()->SetOptions(options);

			// Draw the option bar
			ui->GetOptionBar()->Draw(*renderer, *fontManager, softwareCursor->GetPosition(),
					ui->GetEditorMenu()->IsActive(), sw);
		}

		void KV6EditorView::DrawRibbon(float sw) {
			client::IFont& font = fontManager->GetSmallGuiFont();
			ColorNP(MakeVector4(0.06F, 0.06F, 0.08F, 1.0F));
			FillRect(0.0F, 0.0F, sw, kRibbonH);

			std::string name = (!filePath.empty()) ? filePath : "(unsaved model)";
			if (HasUnsavedChanges())
				name += " *";
			font.Draw("KV6 Editor", MakeVector2(12.0F, 4.0F), 0.95F, MakeVector4(1, 1, 1, 1));
			font.Draw(name + "   (" + std::to_string(DocumentVoxelCount()) + " voxels)",
			          MakeVector2(120.0F, 5.0F), 0.85F, MakeVector4(0.75F, 0.75F, 0.78F, 1.0F));

			std::string cam = "[Ctrl+S] save";
			Vector2 cs = font.Measure(cam);
			font.Draw(cam, MakeVector2(sw - 12.0F - cs.x * 0.8F, 5.0F), 0.8F,
			          MakeVector4(0.6F, 0.6F, 0.63F, 1.0F));
		}

		// --- Pause menu / Save As prompt -------------------------------------

		// --- View interface ---------------------------------------------------

		void KV6EditorView::MouseEvent(float dx, float dy) {
			if (lookActive && shiftHeld) { // Shift + wheel-button drag pans
				// The cursor travels with the drag, staying on the grabbed spot
				// (it stops at the screen edge while the pan carries on).
				softwareCursor->Accumulate(dx, dy);
				PanView(dx, dy);
				return;
			}
			if (lookActive) {
				camAnim = false; // manual look cancels a navicube animation
				float sens = 0.003F;
				yaw += dx * sens;
				pitch -= dy * sens;
				float lim = M_PI_F * 0.5F - 0.01F;
				pitch = Clampf(pitch, -lim, lim);
				return;
			}
			softwareCursor->Accumulate(dx, dy);
			const Vector2& cursor = softwareCursor->GetPosition();

			// While a dialog is up the cursor drives its widgets, not the editor.
			if (ui->GetOverlay()->IsActive())
				return;

			// Forward mouse motion to color picker if it's open
			if (ui && ui->GetColorPicker()->IsOpen()) {
				ui->GetColorPicker()->MouseMove(cursor);
			}

			// Forward cursor motion over the viewport to the active tool as a Move
			// (no button) or Drag (button held) event.
			if (cursor.y < BarsH())
				return;
			PointerButton b = lmbHeld ? PointerButton::Left
			                          : (rmbHeld ? PointerButton::Right : PointerButton::None);
			PointerPhase ph = (lmbHeld || rmbHeld) ? PointerPhase::Drag : PointerPhase::Move;
			DispatchPointer(MakePointer(b, ph, MakeVector2(dx, dy)));
		}

		void KV6EditorView::WheelEvent(float x, float y) {
			if (ui->GetOverlay()->WheelEvent(x, y))
				return;
			if (ui->GetEditorMenu()->IsActive())
				return;
			orbitDist = Clampf(orbitDist * (1.0F + y * 0.1F), 2.0F, 1000.0F);
		}

		void KV6EditorView::KeyEvent(const std::string& key, bool down) {
			const Vector2& cursor = softwareCursor->GetPosition();

			// Modifier state must track the keyboard even while a modal swallows
			// keys, or a Ctrl released during the menu stays "held" afterwards.
			if (key == "Control") ctrlHeld = down;
			if (key == "Alt") altHeld = down;
			if (key == "Shift") shiftHeld = down;
			if (KV6CheckKey(cg_keySprint, key)) keySprint = down;

			// A modal dialog (file browser) owns every key while it is up.
			if (ui->GetOverlay()->KeyEvent(key, down))
				return;

			if (ui->GetEditorMenu()->KeyEvent(key, down)) {
				// Releases are swallowed too while the menu is up: drop held
				// movement/look so nothing keeps going once it closes.
				ReleaseHeldInput();
				return;
			}

			if (down && ctrlHeld && IsCtrlShortcut(key)) {
				// Ctrl is also the default descend key (cg_keyCrouch), so the view
				// has been sinking since Ctrl went down: undo that drift and stop
				// descending for the rest of this press.
				if (KV6CheckKey(cg_keyCrouch, "Control")) {
					orbitTarget -= ctrlDescent;
					ctrlDescent = MakeVector3(0, 0, 0);
					keyDown = false;
				}
				// A shortcut changes the document or the placement under a drag
				// in progress (a paste replaces it), so that drag is abandoned.
				CancelToolInteraction();
				if (EqualsIgnoringCase(key, "s")) Save();
				else if (EqualsIgnoringCase(key, "c")) CopySelection();
				else if (EqualsIgnoringCase(key, "x")) CutSelection();
				else if (EqualsIgnoringCase(key, "v")) Paste();
				else if (EqualsIgnoringCase(key, "z")) { if (shiftHeld) Redo(); else Undo(); }
				else Redo(); // "y"
				return;
			}

			if (key == "MiddleMouseButton") { lookActive = down; return; }

			if (key == "LeftMouseButton") {
				if (!down) {
					lmbHeld = false;
					if (ui)
						ui->GetColorPicker()->MouseUp();
					DispatchPointer(MakePointer(PointerButton::Left, PointerPhase::Up));
					return;
				}

				// Check if color picker handles this click
				if (ui && ui->GetColorPicker()->IsOpen()) {
					ColorPicker::ClickResult result = ui->GetColorPicker()->HitTest(cursor);
					if (result.type != ColorPicker::ClickType::None) {
						if (result.type == ColorPicker::ClickType::Close) {
							ui->GetColorPicker()->Close();
						} else {
							ui->GetColorPicker()->MouseDown(cursor);
						}
						return;
					}
				}

				// Over the bars, a click belongs to whichever button is under it
				// (the callbacks wired in the constructor act on it), or to nothing.
				if (cursor.y < BarsH()) {
					if (!ui->GetToolbar()->Click(cursor, screenWidth))
						ui->GetOptionBar()->Click(cursor);
					return;
				}

				// Check navigation cube
				Vector3 navDir;
				if (NaviCubeDir(cursor, navDir)) { SnapCameraDir(navDir); return; }

				// Sampling a colour works the same in every tool, which never sees
				// the click.
				if (SamplingArmed()) {
					SampleColor();
					return;
				}

				// Otherwise forward to active tool
				lmbHeld = true;
				DispatchPointer(MakePointer(PointerButton::Left, PointerPhase::Down));
				return;
			}
			if (key == "RightMouseButton") {
				if (!down) {
					rmbHeld = false;
					DispatchPointer(MakePointer(PointerButton::Right, PointerPhase::Up));
					return;
				}
				// Don't allow RMB actions over the UI bars or color picker
				if (ui && ui->GetColorPicker()->IsOverPicker(cursor)) return;
				if (cursor.y < BarsH()) return; // over the bars
				rmbHeld = true;
				DispatchPointer(MakePointer(PointerButton::Right, PointerPhase::Down));
				return;
			}

			if (down && KV6CheckKey(cg_keyScreenshot, key)) { wantScreenShot = true; return; }
			if (down && KV6CheckKey(cg_keyDelete, key)) {
				// Like a Ctrl shortcut, it edits behind the tool's back.
				CancelToolInteraction();
				DeleteSelection();
				return;
			}

			std::string fwd = cg_keyMoveForward, bk = cg_keyMoveBackward, lf = cg_keyMoveLeft;
			std::string rt = cg_keyMoveRight, jp = cg_keyJump, cr = cg_keyCrouch;
			if (KV6CheckKey(fwd, key)) { keyFwd = down; return; }
			if (KV6CheckKey(bk, key)) { keyBack = down; return; }
			if (KV6CheckKey(lf, key)) { keyLeft = down; return; }
			if (KV6CheckKey(rt, key)) { keyRight = down; return; }
			if (KV6CheckKey(jp, key)) { keyUp = down; return; }
			if (KV6CheckKey(cr, key)) {
				keyDown = down;
				if (down)
					descendPressTime = globalTime; // starts the grace period
				ctrlDescent = MakeVector3(0, 0, 0); // a new press, or a finished one
				return;
			}

			// Tool and sub-tool keys, shown on their buttons. Plain keys only, so
			// a chord never switches tools.
			if (down && !ctrlHeld && !altHeld && SwitchByHotKey(key))
				return;

			// Remaining keys go to the active tool (e.g. Select's [L]).
			if (EditorTool* t = ActiveTool()) {
				KeyInput e;
				e.key = key;
				e.phase = down ? KeyPhase::Down : KeyPhase::Up;
				e.alt = altHeld;
				e.ctrl = ctrlHeld;
				e.shift = shiftHeld;
				// A key press is one user action of its own, unless it comes during
				// a press of a mouse button, whose action it then joins.
				const bool ownAction = !lmbHeld && !rmbHeld;
				if (ownAction)
					undo.BeginAction();
				UserActionEnd end(*this, ownAction);
				t->OnKey(*this, e);
			}
		}

		void KV6EditorView::TextInputEvent(const std::string& text) {
			if (ui->GetOverlay()->IsActive()) {
				ui->GetOverlay()->TextInputEvent(text);
				return;
			}
			ui->GetEditorMenu()->TextInputEvent(text);
		}

		void KV6EditorView::TextEditingEvent(const std::string& text, int start, int len) {
			ui->GetOverlay()->TextEditingEvent(text, start, len);
		}

		bool KV6EditorView::AcceptsTextInput() {
			return ui->GetOverlay()->AcceptsTextInput() || ui->GetEditorMenu()->AcceptsTextInput();
		}

		AABB2 KV6EditorView::GetTextInputRect() {
			if (ui->GetOverlay()->IsActive())
				return ui->GetOverlay()->GetTextInputRect();
			return ui->GetEditorMenu()->GetTextInputRect();
		}

		void KV6EditorView::RunFrame(float dt) {
			SPADES_MARK_FUNCTION();
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();
			globalTime += dt;

			// Smoothly rotate toward a navicube-selected view.
			if (camAnim) {
				float k = std::min(1.0F, dt * 12.0F);
				yaw += (targetYaw - yaw) * k;
				pitch += (targetPitch - pitch) * k;
				if (std::fabs(targetYaw - yaw) < 0.002F && std::fabs(targetPitch - pitch) < 0.002F) {
					yaw = targetYaw;
					pitch = targetPitch;
					camAnim = false;
				}
			}

			if (!ui->GetEditorMenu()->IsActive() && !ui->GetOverlay()->IsActive())
				UpdateMovement(dt);
			if (statusTimer > 0.0F)
				statusTimer -= dt;

			renderer->SetFogColor(MakeVector3(0.10F, 0.10F, 0.12F));
			// Matches the far plane: fog that ended closer would swallow the model
			// at the far end of the zoom range.
			renderer->SetFogDistance(ViewDistance());

			// The scene is rendered full-screen (sub-viewport rendering isn't
			// guaranteed across renderers — keep this renderer-agnostic), and the
			// ribbon + toolbar bars are drawn opaque over the top. So the camera
			// projection and the cursor->ray pick both use the full screen.
			client::SceneDefinition sceneDef = SetupScene(0.0F, 0.0F, sw, sh);
			camera.eye = sceneDef.viewOrigin;
			camera.right = sceneDef.viewAxis[0];
			camera.up = sceneDef.viewAxis[1];
			camera.forward = sceneDef.viewAxis[2];
			camera.tanHalfFovX = tanf(sceneDef.fovX * 0.5F);
			camera.tanHalfFovY = tanf(sceneDef.fovY * 0.5F);
			camera.viewportX = 0.0F;
			camera.viewportY = 0.0F;
			camera.viewportWidth = sw;
			camera.viewportHeight = sh;

			renderer->StartScene(sceneDef);
			if (renderModel) {
				client::ModelRenderParam param;
				// Cancel the KV6 pivot so the model sits in the editor's grid space.
				param.matrix = Matrix4::Translate(model->GetOrigin() * -1.0F);
				renderer->RenderModel(*renderModel, param);
			}
			// Pending voxels (a paste, an import, or a selection being moved) render
			// solid at their temporary position, in a document that no longer holds
			// them.
			if (placementActive && placementModel) {
				client::ModelRenderParam param;
				param.matrix = Matrix4::Translate(MakeVector3(float(placement.anchor.x),
				                                             float(placement.anchor.y),
				                                             float(placement.anchor.z)));
				renderer->RenderModel(*placementModel, param);
			}
			DrawHelpers();
			DrawOriginAxes();
			DrawMirrorPlanes();
			DrawSelection();

			EditorTool* tool = ActiveTool();
			if (tool)
				tool->DrawScene(*this);
			// Voxels waiting to be placed, drawn in their own colours.
			if (placementActive)
				DrawPlacementPreview();
			renderer->EndScene();

			DrawOverlayLines2D(); // dim see-through pass for occluded outlines/gizmo
			// The picker is laid out first: the text under the viewport keeps clear of it.
			ui->GetColorPicker()->UpdateLayout(sw, sh, BarsH());
			DrawOverlay(sw, sh);
			DrawRibbon(sw);
			DrawToolbar(sw, sh);
			DrawSubToolbar(sw);

			// Layout and draw navigation cube
			float gizBox = 174.0F;
			gizR = gizBox * 0.5F - 14.0F;
			gizCx = sw - 16.0F - gizBox * 0.5F;
			gizCy = BarsH() + 8.0F + gizBox * 0.5F;
			DrawNaviCube();

			// Draw color picker
			if (ui) {
				ui->GetColorPicker()->Draw(*renderer, *fontManager, softwareCursor->GetPosition());
			}

			if (tool)
				tool->DrawOverlay(*this);

			ui->GetEditorMenu()->Draw();

			// Dialogs draw their own pointer (with the text-field I-beam), so the
			// editor's cursor steps aside while one is open.
			ui->GetOverlay()->RunFrame(dt);
			ui->GetOverlay()->Draw();
			if (!ui->GetOverlay()->IsActive())
				softwareCursor->Draw();

			// The runner is the only thing that presents, so the capture happens here
			// rather than after the frame: `FrameDone` flushes everything drawn above so
			// the read-back is a complete image. Same shape as `Client::TakeScreenShot`.
			// Shares the client's Screenshots/ numbering and cg_screenshotFormat.
			if (wantScreenShot) {
				wantScreenShot = false;
				renderer->FrameDone();
				try {
					std::string name = client::SaveScreenShot(*renderer);
					SetStatus("Screenshot saved: " + name);
				} catch (const std::exception& ex) {
					SetStatus("Screenshot failed");
					SPLog("Editor screenshot failed: %s", ex.what());
				}
			}
		}
	} // namespace gui
} // namespace spades
