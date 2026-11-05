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

#include "VoxelEditor.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <unordered_map>
#include <tuple>
#include <utility>

#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IModel.h>
#include <Client/IRenderer.h>
#include <Core/Bitmap.h>
#include <Core/Debug.h>
#include <Core/LocalFileSystem.h>
#include <Core/StdStream.h>
#include <Core/Settings.h>
#include <Core/VoxelModel.h>
#include <Gui/OverlayPaint.h>
#include <Gui/UI/Components/ColorPicker.h>
#include <Gui/UI/Components/ContextMenu.h>
#include <Gui/UI/Components/MenuBar.h>
#include <Gui/UI/Components/Gizmo/GizmoCanvas.h>
#include <Gui/UI/Components/Gizmo/TransformGizmo.h>
#include <Gui/UI/Components/OptionBar.h>
#include <Gui/UI/Components/SoftwareCursor.h>
#include <Gui/UI/Components/Toolbar.h>
#include <Gui/UI/Components/TransformPanel.h>
#include <Gui/UI/Widgets/TextEntry.h>
#include <Gui/UI/Components/UIOverlayHost.h>
#include <Gui/UI/Editor/Shell/EditorKeys.h>
#include <Gui/UI/Editor/Shell/EditorUI.h>

#include "DrawTool.h"
#include "ObjectModeHost.h"
#include "ObjectTool.h"
#include "SubTool.h"
#include "VoxelDocument.h"
#include "VoxelEditorHost.h"

DEFINE_SPADES_SETTING(cg_keyDelete, "Delete");

namespace spades {
	namespace gui {
		namespace {
			// `v` in whole half-voxel steps, to the nearest step with ties going up.
			// A mirror plane `k` half steps from the origin maps voxel i to k - i,
			// and the plane snaps to the same count, so snapping never changes
			// which voxels a plane pairs up.
			int HalfSteps(float v) { return int(std::floor(v * 2.0F + 0.5F)); }

			// Top UI bands (full width): the host's title ribbon, the toolbar and
			// the option bar (the active tool's sub-tools). The viewport starts
			// below them.
			const float kRibbonH = 24.0F;
			const float kToolbarH = 32.0F;
			const float kSubBarH = 28.0F;
			const float kBarsH = kRibbonH + kToolbarH + kSubBarH;

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

			// The six voxels sharing a face with a voxel, as offsets.
			const IntVector3 kFaceNeighbours[6] = {
			  MakeIntVector3(1, 0, 0),  MakeIntVector3(-1, 0, 0), MakeIntVector3(0, 1, 0),
			  MakeIntVector3(0, -1, 0), MakeIntVector3(0, 0, 1),  MakeIntVector3(0, 0, -1)};

			// The smallest box holding `cells`; false when there are none.
			bool BoundsOf(const std::vector<IntVector3>& cells, IntVector3& lo, IntVector3& hi) {
				if (cells.empty())
					return false;
				lo = hi = cells.front();
				for (const IntVector3& c : cells) {
					lo = MakeIntVector3(std::min(lo.x, c.x), std::min(lo.y, c.y), std::min(lo.z, c.z));
					hi = MakeIntVector3(std::max(hi.x, c.x), std::max(hi.y, c.y), std::max(hi.z, c.z));
				}
				return true;
			}
					// The toolbar id of Object mode's own tool, which no registered
			// tool takes.
			const char* const kObjectToolId = "object";
} // namespace

		/**
		 * The model's pivot, as the transform panel sets it: a Location only,
		 * previewed live and kept as one Set Pivot step.
		 */
		class VoxelEditor::PivotTarget final : public ITransformTarget {
		public:
			explicit PivotTarget(VoxelEditor& editor) : editor(editor) {}

			unsigned ShownRows() const override { return kLocation; }
			std::string Title() const override { return "Pivot"; }

			bool CurrentTransform(Transform& t, std::string& name, bool& onlyMoves) const override {
				t.position = editor.GetPivot();
				t.rotation = MakeVector3(0.0F, 0.0F, 0.0F);
				t.scale = MakeVector3(1.0F, 1.0F, 1.0F);
				name = "of the " + editor.document.Noun();
				onlyMoves = true;
				return true;
			}
			void PreviewTransform(const Transform& t) override {
				previewing = true;
				value = t.position;
				editor.PreviewPivot(value);
			}
			void CommitTransform() override {
				if (!previewing)
					return;
				previewing = false;
				editor.EndPreviews(); // where it was, so the step runs from there
				editor.SetPivot(value);
			}
			void CancelTransform() override {
				if (!previewing)
					return;
				previewing = false;
				editor.EndPreviews();
			}

		private:
			VoxelEditor& editor;
			bool previewing = false;
			Vector3 value = MakeVector3(0.0F, 0.0F, 0.0F);
		};

		VoxelEditor::VoxelEditor(IVoxelDocument& document, IVoxelEditorHost& host,
		                         client::IRenderer& renderer, client::FontManager& fontManager,
		                         EditorUI& ui, SoftwareCursor& cursor, const Config& config)
		    : document(document),
		      host(host),
		      renderer(&renderer),
		      fontManager(&fontManager),
		      ui(ui),
		      cursor(cursor),
		      undo(*this, config.undo),
		      placeAtAim(config.placeAtAim) {
			SPADES_MARK_FUNCTION();
			transformPanel.reset(new TransformPanel(renderer, fontManager.GetSmallGuiFont()));
			pivotTarget.reset(new PivotTarget(*this));

			objectTool = std::make_unique<ObjectTool>();

			// The tools come from the registry (toolbar order = registration),
			// narrowed to the ones this editor offers.
			VoxelTools().BuildAll(tools);
			if (!config.tools.empty()) {
				tools.erase(std::remove_if(tools.begin(), tools.end(),
				                           [&](const VoxelToolSlot& slot) {
					                           return std::find(config.tools.begin(),
					                                            config.tools.end(),
					                                            slot.id) == config.tools.end();
				                           }),
				            tools.end());
			}
			// Open on Draw, so the user can start at once; found by type, so no
			// button label decides it.
			activeTool = 0;
			for (size_t i = 0; i < tools.size(); i++) {
				if (dynamic_cast<DrawTool*>(tools[i].tool.get())) {
					activeTool = int(i);
					break;
				}
			}
			defaultTool = activeTool;
			WireUI();
		}

		VoxelEditor::~VoxelEditor() { SPADES_MARK_FUNCTION(); }

		ITransformTarget& VoxelEditor::PivotTransform() { return *pivotTarget; }

		ITransformTarget* VoxelEditor::PanelTarget() {
			VoxelTool* t = ActiveTool();
			return t ? t->TransformTarget(*this) : nullptr;
		}

		void VoxelEditor::SettlePanel() {
			if (!transformPanel->IsTyping() && !transformPanel->IsHolding())
				return;
			if (ITransformTarget* target = PanelTarget())
				transformPanel->Settle(*target);
		}

		bool VoxelEditor::IsTyping() const {
			return transformPanel->IsTyping() || BarsEditing();
		}

		bool VoxelEditor::HoldsPointer() const { return transformPanel->IsHolding(); }

		bool VoxelEditor::OnPanel(const Vector2& at) {
			return transformPanel->Contains(at) && !ui.GetColorPicker()->IsOverPicker(at);
		}

		void VoxelEditor::TextInput(const std::string& text) {
			if (transformPanel->IsTyping()) {
				TextEntry::Modifiers m;
				m.ctrl = ctrlHeld;
				m.alt = altHeld;
				m.meta = metaHeld;
				if (TextEntry::Types(m)) // not the letter of a shortcut
					transformPanel->Insert(text);
				return;
			}
			ui.GetOptionBar()->TextInputEvent(text); // whichever is being typed into
			ui.GetContextStrip()->TextInputEvent(text);
		}

		bool VoxelEditor::BarsEditing() const {
			return ui.GetOptionBar()->IsEditing() || ui.GetContextStrip()->IsEditing();
		}

		void VoxelEditor::EndBarEdits(bool commit) {
			ui.GetOptionBar()->EndEditing(commit);
			ui.GetContextStrip()->EndEditing(commit);
		}

		void VoxelEditor::TextComposition(const std::string& text) {
			if (transformPanel->IsTyping())
				transformPanel->Compose(text);
		}

		bool VoxelEditor::AcceptsTextInput() const { return IsTyping(); }

		AABB2 VoxelEditor::TextInputRect() const {
			if (transformPanel->IsTyping())
				return transformPanel->TextRect();
			return ui.GetContextStrip()->IsEditing() ? ui.GetContextStrip()->EditingRect()
			                                         : ui.GetOptionBar()->EditingRect();
		}

		void VoxelEditor::WireUI() {
			// Buttons report ids, resolved against the modes and tools as they are
			// now; the toolbar only reports clicks on enabled buttons.
			ui.GetToolbar()->OnModeSelectorClicked = [this](const AABB2& anchor) {
				OpenModeMenu(anchor);
			};
			ui.GetToolbar()->OnToolClicked = [this](const std::string& id) {
				if (currentMode == EditorMode::Object && id == kObjectToolId)
					SetObjectModeTool(-1);
				else
					SetActiveTool(ToolIndex(id));
			};
			ui.GetToolbar()->OnUndoClicked = [this]() { Undo(); };
			ui.GetToolbar()->OnRedoClicked = [this]() { Redo(); };

			// The option bar's content belongs to the tool whose bar it is, the
			// strip's to the tool it is the strip of; a click only counts while
			// that tool still is the one, and goes to it whichever is active.
			WireBar(*ui.GetOptionBar(), [this] { return BarToolIndex(); });
			WireBar(*ui.GetContextStrip(), [this] { return StripToolIndex(); });

			// The brush colour is the editor's, shared by every tool: its swatch
			// sits on the main toolbar and opens or closes the picker, which
			// edits it and stays up across tool switches until closed.
			ui.GetToolbar()->OnColorSwatchClicked = [this] {
				ColorPicker& picker = *ui.GetColorPicker();
				if (picker.IsOpen()) {
					picker.Close();
				} else {
					picker.SetColor(PickerColor());
					picker.Open();
				}
			};
			// The active tool follows the colour as it is chosen (Select
			// recolours the selection with it), live while the picker is pressed.
			ui.GetColorPicker()->OnColorChanged = [this](std::uint32_t c) {
				// A tool with a colour of its own for the picker has it set
				// and that is all: the brush, and what follows it, stay put.
				std::uint32_t& target = PickerColor();
				target = c;
				if (&target != &currentColor)
					return;
				if (pickerHeld)
					pickerChangedColor = true;
				BrushColorChanged(!pickerHeld);
			};
		}

		std::uint32_t& VoxelEditor::PickerColor() {
			if (VoxelTool* t = ActiveTool()) {
				if (std::uint32_t* own = t->PickerColor())
					return *own;
			}
			return currentColor;
		}

		void VoxelEditor::WireBar(OptionBar& bar, std::function<int()> indexOf) {
			auto toolOf = [this, indexOf]() -> VoxelTool* {
				const int index = indexOf();
				return index < 0 ? nullptr : tools[static_cast<size_t>(index)].tool.get();
			};
			bar.CurrentOwner = [toolOf] { return static_cast<const void*>(toolOf()); };
			bar.OnSubToolClicked = [this, indexOf](int sub) {
				const int index = indexOf();
				SetActiveTool(index); // says why not, when it has nothing to work on
				if (index >= 0 && activeTool == index)
					tools[static_cast<size_t>(index)].tool->SetSubTool(*this, sub);
			};
			bar.OnBoolToggled = [this, toolOf](const std::string& id) {
				VoxelTool* tool = toolOf();
				if (ToolOption* opt = FindOption(tool, id, ToolOption::Type::Bool)) {
					opt->bvalue = !opt->bvalue;
					tool->OnOptionToggled(*this, id, opt->bvalue);
				}
			};
			bar.OnActionClicked = [this, toolOf](const std::string& id) {
				VoxelTool* tool = toolOf();
				const ToolOption* opt = FindOption(tool, id, ToolOption::Type::Action);
				if (!opt || !opt->enabled)
					return;
				// A command of a tool that is not the active one runs behind
				// the active one's back, as a command from a menu does.
				if (tool != ActiveTool())
					CancelToolInteraction();
				tool->OnAction(*this, id);
			};
			bar.OnNumberChanged = [this, toolOf](const std::string& id, float value,
			                                     bool committed) {
				VoxelTool* tool = toolOf();
				if (!FindOption(tool, id, ToolOption::Type::Number))
					return;
				// A settled value is one user action, so a typed number or an
				// arrow press undoes in one go; an unsettled one only previews
				// and has nothing to record.
				if (committed)
					undo.BeginAction();
				UserActionEnd end(*this, committed);
				tool->OnOptionNumberChanged(*this, id, value, committed);
			};
		}

		void VoxelEditor::BrushColorChanged(bool committed) {
			if (VoxelTool* t = ActiveTool())
				t->OnBrushColorChanged(*this, currentColor, committed);
		}

		void VoxelEditor::EndPickerPress(bool commit) {
			pickerHeld = false;
			ui.GetColorPicker()->MouseUp();
			const bool changed = pickerChangedColor;
			pickerChangedColor = false;
			// A press of the other button began the action this one joined, and
			// ends it on its own release.
			UserActionEnd end(*this, !rmbHeld);
			if (commit && changed)
				BrushColorChanged(true);
		}

		float VoxelEditor::RibbonHeight() { return kRibbonH; }
		float VoxelEditor::BarsHeight() { return kBarsH; }

		// --- Document ---------------------------------------------------------

		void VoxelEditor::DocumentReplaced() {
			// Nothing of the old document's edit state carries over but which
			// axes mirror: that is how the user works, not part of the document.
			MirrorSetup kept;
			for (int a = 0; a < 3; a++)
				kept.enabled[a] = edit.mirror.enabled[a];
			edit = EditState();
			edit.mirror = kept;
			movedMiddle = SelectionMiddle();
			previewedOrigin.reset();
			previewedMirrorPlane.reset();
			// What a live recolour painted over was the old document's.
			previewedColors.reset();
			previewedPlacement.reset();
			undo.Clear();
			// The mirror planes start on the new document's pivot.
			PlaceMirrorPlane(GetPivot());
			NoteDocumentColors();
			NotifyDocumentChanged();
		}

		bool VoxelEditor::InBounds(int x, int y, int z) const {
			const IntVector3 size = document.Size();
			return x >= 0 && y >= 0 && z >= 0 && x < size.x && y < size.y && z < size.z;
		}

		bool VoxelEditor::Editable(int x, int y, int z) const {
			return InBounds(x, y, z) && document.IsEditable(x, y, z);
		}

		void VoxelEditor::SetStatus(const std::string& s) {
			statusMessage = s;
			statusTimer = 2.5F;
		}

		std::string VoxelEditor::SizeLimitMessage() const {
			return "Reached the maximum " + document.Noun() + " size";
		}

		std::string VoxelEditor::SizeLimitMessage(const IntVector3& wanted) const {
			// Which way it ran out, and by how much. "Too large" leaves the user
			// to guess which of three limits they met and how far off they are,
			// and the three are not alike: a model may be thousands of voxels
			// across and only a few dozen deep.
			const IntVector3 limit = document.MaxSize();
			const char* const axes[3] = {"X", "Y", "Z"};
			const int want[3] = {wanted.x, wanted.y, wanted.z};
			const int most[3] = {limit.x, limit.y, limit.z};
			for (int a = 0; a < 3; a++) {
				if (want[a] > most[a]) {
					return "A " + document.Noun() + " reaches " + std::to_string(most[a]) +
					       " voxels along " + axes[a] + ", and this would need " +
					       std::to_string(want[a]);
				}
			}
			return SizeLimitMessage();
		}

		// --- Escape -----------------------------------------------------------

		VoxelEditor::EscapeLayer VoxelEditor::NextEscape() {
			// A number is a number wherever it is being typed: the panel's, and
			// the bar's boxes, which answer for their own Escape through
			// OptionBar but never see it once Escape has somewhere else to go.
			if (transformPanel->IsTyping() || transformPanel->IsHolding() ||
			    BarsEditing())
				return EscapeLayer::Number;
			if (ui.GetColorPicker()->GetEyedropperMode())
				return EscapeLayer::Eyedropper;
			if (VoxelTool* tool = ActiveTool()) {
				if (!tool->EscapeLabel(*this).empty())
					return EscapeLayer::Tool;
			}
			// The pending voxels and the selection are Edit mode's, out of reach
			// of Object mode's Escape.
			if (currentMode != EditorMode::Edit)
				return EscapeLayer::None;
			if (edit.placing)
				return EscapeLayer::Placement;
			if (!edit.selection.Empty())
				return EscapeLayer::Selection;
			return EscapeLayer::None;
		}

		std::string VoxelEditor::EscapeLabel(EscapeLayer layer) {
			switch (layer) {
				case EscapeLayer::Number: return "give the number up";
				case EscapeLayer::Eyedropper: return "stop picking";
				case EscapeLayer::Tool: return ActiveTool()->EscapeLabel(*this);
				case EscapeLayer::Placement: return "drop the pending voxels";
				case EscapeLayer::Selection: return "select nothing";
				case EscapeLayer::None: break;
			}
			// Nothing in hand: what Escape does then is the host's to decide
			// (this one leaves, asking about unsaved work first).
			return "exit to menu";
		}

		void VoxelEditor::Escape(EscapeLayer layer) {
			switch (layer) {
				case EscapeLayer::Number:
					if (BarsEditing()) {
						EndBarEdits(false); // what was typed is given up
						break;
					}
					if (ITransformTarget* target = PanelTarget()) {
						if (!transformPanel->CancelScrub(*target))
							transformPanel->Key(*target, "Escape", TextEntry::Modifiers(), clock);
					}
					break;
				case EscapeLayer::Eyedropper: ui.GetColorPicker()->SetEyedropperMode(false); break;
				case EscapeLayer::Tool: ActiveTool()->OnEscape(*this); break;
				case EscapeLayer::Placement: CancelPlacement(); break;
				case EscapeLayer::Selection:
					ClearSelection();
					SetStatus("Selection cleared");
					break;
				case EscapeLayer::None: break;
			}
		}

		bool VoxelEditor::EscapeOnce() {
			const EscapeLayer layer = NextEscape();
			Escape(layer);
			return layer != EscapeLayer::None;
		}

		// --- Picking ----------------------------------------------------------

		Vector3 VoxelEditor::ViewDir() const { return host.CurrentView().forward; }

		GizmoView VoxelEditor::DocumentView() const {
			// The same view, with the world shifted so voxel i is centred at i.
			GizmoView view = host.CurrentView();
			view.eye = view.eye - host.DocumentOrigin();
			return view;
		}
		const Vector2& VoxelEditor::CursorPos() const { return host.AimPosition(); }

		bool VoxelEditor::RayPlaneCell(const Vector3& planePoint, const Vector3& normal,
		                               IntVector3& out) {
			const GizmoView view = DocumentView();
			if (!view.IsValid())
				return false;
			Vector3 origin, dir;
			view.Ray(host.AimPosition(), origin, dir);
			float denom = Vector3::Dot(dir, normal);
			if (std::fabs(denom) < 1.0e-5F)
				return false;
			float t = Vector3::Dot(planePoint - origin, normal) / denom;
			if (t <= 0.0F)
				return false;
			Vector3 hit = origin + dir * t;
			// A ray grazing the plane meets it arbitrarily far away; no voxel lies
			// there, and the cell would not even fit in an int. No cell the editor
			// works with lies farther out than a few times the largest document.
			const IntVector3 maxSize = document.MaxSize();
			const float reach =
			  float(std::max(maxSize.x, std::max(maxSize.y, maxSize.z))) * 4.0F;
			for (float c : {hit.x, hit.y, hit.z}) {
				if (!std::isfinite(c) || std::fabs(c) > reach)
					return false;
			}
			out = MakeIntVector3(int(std::floor(hit.x + 0.5F)), int(std::floor(hit.y + 0.5F)),
			                     int(std::floor(hit.z + 0.5F)));
			return true;
		}

		void VoxelEditor::DoPick() {
			pickHit = false;
			const GizmoView view = DocumentView();
			if (!view.IsValid())
				return;
			Vector3 eye, dir;
			view.Ray(host.AimPosition(), eye, dir);
			pickHit = FirstSolidAlong(eye, dir, pickSolid, pickPlace);
		}

		bool VoxelEditor::FirstSolidAlong(const Vector3& eye, const Vector3& dir, IntVector3& solid,
		                                  IntVector3& before) const {
			// The renderer centres voxel (x,y,z) at world (x,y,z), so traverse in a
			// +0.5-shifted space where each integer cell maps to a voxel index.
			Vector3 o = eye + MakeVector3(0.5F, 0.5F, 0.5F);

			// Only the volume can hold a voxel: clip the ray to its box and walk
			// from where it enters. Walking from the eye instead would cost steps
			// in proportion to the camera's distance, and with any fixed budget a
			// small volume seen from afar would never be reached at all.
			const IntVector3 extent = document.Size();
			const float size[3] = {float(extent.x), float(extent.y), float(extent.z)};
			const float from[3] = {o.x, o.y, o.z};
			const float along[3] = {dir.x, dir.y, dir.z};
			float tEnter = 0.0F, tExit = std::numeric_limits<float>::infinity();
			for (int a = 0; a < 3; a++) {
				if (along[a] == 0.0F) {
					if (from[a] < 0.0F || from[a] > size[a])
						return false; // parallel to this slab and outside it
					continue;
				}
				float t0 = (0.0F - from[a]) / along[a];
				float t1 = (size[a] - from[a]) / along[a];
				if (t0 > t1)
					std::swap(t0, t1);
				tEnter = std::max(tEnter, t0);
				tExit = std::min(tExit, t1);
			}
			if (tEnter > tExit)
				return false; // the ray misses the volume
			// The cell before the first one inside is the one a voxel is placed
			// in when the hit is on the volume's own face.
			const float kInside = 1.0e-4F;
			o = o + dir * std::max(0.0F, tEnter - kInside);

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

			float tMaxX =
			  (dir.x != 0.0F) ? ((float(cx) + (stepX > 0 ? 1.0F : 0.0F) - o.x) / dir.x) : big;
			float tMaxY =
			  (dir.y != 0.0F) ? ((float(cy) + (stepY > 0 ? 1.0F : 0.0F) - o.y) / dir.y) : big;
			float tMaxZ =
			  (dir.z != 0.0F) ? ((float(cz) + (stepZ > 0 ? 1.0F : 0.0F) - o.z) / dir.z) : big;

			// A straight line crosses at most one cell per unit of each axis, so
			// this many steps take it from one side of the volume to the other.
			int px = cx, py = cy, pz = cz;
			const int limit = extent.x + extent.y + extent.z + 3;
			for (int i = 0; i < limit; i++) {
				if (document.IsSolid(cx, cy, cz)) {
					solid = MakeIntVector3(cx, cy, cz);
					before = MakeIntVector3(px, py, pz);
					return true;
				}
				px = cx;
				py = cy;
				pz = cz;
				if (tMaxX <= tMaxY && tMaxX <= tMaxZ) {
					cx += stepX;
					tMaxX += tDeltaX;
				} else if (tMaxY <= tMaxZ) {
					cy += stepY;
					tMaxY += tDeltaY;
				} else {
					cz += stepZ;
					tMaxZ += tDeltaZ;
				}
			}
			return false;
		}

		// --- Volume -----------------------------------------------------------

		VoxelEditor::VolumeFrame VoxelEditor::FrameHolding(const IntVector3& lo,
		                                                   const IntVector3& hi) const {
			const IntVector3 size = document.Size();
			VolumeFrame frame;
			if (!document.Resizable()) {
				// A fixed volume holds what it holds; anything past it is clipped.
				frame.size = size;
				frame.shift = MakeIntVector3(0, 0, 0);
				return frame;
			}
			const IntVector3 from =
			  MakeIntVector3(std::min(0, lo.x), std::min(0, lo.y), std::min(0, lo.z));
			const IntVector3 to = MakeIntVector3(std::max(size.x, hi.x + 1),
			                                     std::max(size.y, hi.y + 1),
			                                     std::max(size.z, hi.z + 1));
			frame.size = to - from;
			frame.shift = MakeIntVector3(-from.x, -from.y, -from.z);
			return frame;
		}

		bool VoxelEditor::Fits(const VolumeFrame& frame) const {
			const IntVector3 limit = document.MaxSize();
			return frame.size.x <= limit.x && frame.size.y <= limit.y && frame.size.z <= limit.z;
		}

		void VoxelEditor::Reframe(const VolumeFrame& frame) {
			if (frame.shift == MakeIntVector3(0, 0, 0) && frame.size == document.Size())
				return;
			RebuildVolume(frame.size, frame.shift);
		}

		void VoxelEditor::RebuildVolume(const IntVector3& size, const IntVector3& shift) {
			const IntVector3 before = document.Size();
			undo.RecordReframe(before.x, before.y, before.z, size.x, size.y, size.z, shift.x,
			                   shift.y, shift.z);
			ReframeRaw(size, shift);
		}

		void VoxelEditor::ReframeRaw(const IntVector3& size, const IntVector3& offset) {
			document.Reframe(size, offset);
			const Vector3 shift = MakeVector3(float(offset.x), float(offset.y), float(offset.z));
			host.DocumentReframed(shift); // the view stays on the relabelled voxels
			// Everything else held in voxel coordinates follows the relabelling:
			// the mirror planes (by whole voxels, so still on the half-step
			// grid), the reference images, the selection, and pending voxels,
			// which would otherwise land in the wrong place.
			edit.mirror.plane += shift;
			// An image is lined up with the voxels it is being modelled from, so
			// it goes where they go rather than letting the model slide out from
			// under it every time the volume grows or is trimmed.
			for (ReferenceImage& ref : references)
				ref.centre += shift;
			// The middle a moved selection turns about goes with it, and stays
			// its middle: the shifted selection is the same one, relabelled.
			const bool middleKept =
			  movedMiddle.valid && movedMiddle.selectionVersion == edit.selection.Version();
			edit.selection.Shift(offset);
			if (middleKept) {
				movedMiddle.selectionVersion = edit.selection.Version();
				movedMiddle.at = movedMiddle.at + offset;
			}
			if (edit.placing) {
				PendingPlacement& pending = edit.placement;
				pending.anchor = pending.anchor + offset;
				pending.pivot = pending.pivot + offset;
			}
		}

		void VoxelEditor::TrimVolume() {
			if (!document.Resizable())
				return;
			const IntVector3 size = document.Size();
			int minX = size.x, minY = size.y, minZ = size.z;
			int maxX = -1, maxY = -1, maxZ = -1;
			for (int x = 0; x < size.x; x++)
				for (int y = 0; y < size.y; y++)
					for (int z = 0; z < size.z; z++) {
						if (!document.IsSolid(x, y, z))
							continue;
						minX = std::min(minX, x);
						minY = std::min(minY, y);
						minZ = std::min(minZ, z);
						maxX = std::max(maxX, x);
						maxY = std::max(maxY, y);
						maxZ = std::max(maxZ, z);
					}
			if (maxX < 0)
				return;
			const IntVector3 trimmed = MakeIntVector3(maxX - minX + 1, maxY - minY + 1, maxZ - minZ + 1);
			if (trimmed == size)
				return;
			RebuildVolume(trimmed, MakeIntVector3(-minX, -minY, -minZ));
		}

		// --- Editing ----------------------------------------------------------

		int VoxelEditor::MirrorIdx(int i, float plane) const { return HalfSteps(plane) - i; }

		void VoxelEditor::ExpandMirrors(std::vector<IntVector3>& cells) const {
			const MirrorSetup& mirror = edit.mirror;
			const bool mx = MirrorEnabled(0), my = MirrorEnabled(1), mz = MirrorEnabled(2);
			if (mx || my || mz) {
				std::vector<IntVector3> out;
				out.reserve(cells.size() * 8);
				for (const IntVector3& c : cells) {
					int xs[2] = {c.x, c.x}, nx = 1;
					int ys[2] = {c.y, c.y}, ny = 1;
					int zs[2] = {c.z, c.z}, nz = 1;
					if (mx) {
						int m = MirrorIdx(c.x, mirror.plane.x);
						if (m != c.x) {
							xs[1] = m;
							nx = 2;
						}
					}
					if (my) {
						int m = MirrorIdx(c.y, mirror.plane.y);
						if (m != c.y) {
							ys[1] = m;
							ny = 2;
						}
					}
					if (mz) {
						int m = MirrorIdx(c.z, mirror.plane.z);
						if (m != c.z) {
							zs[1] = m;
							nz = 2;
						}
					}
					for (int a = 0; a < nx; a++)
						for (int b = 0; b < ny; b++)
							for (int d = 0; d < nz; d++)
								out.push_back(MakeIntVector3(xs[a], ys[b], zs[d]));
				}
				cells.swap(out);
			}
			// A cell and its image can both be among the cells (a box across a
			// plane): each is listed once, so counts of what an edit touches hold.
			auto before = [](const IntVector3& a, const IntVector3& b) {
				return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
			};
			std::sort(cells.begin(), cells.end(), before);
			cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
		}

		void VoxelEditor::PlaceCube() {
			DocumentCommand command(*this); // pending voxels land first, then the pick sees them
			DoPick();
			if (pickHit)
				Fill(std::vector<IntVector3>(1, PickPlace()), currentColor, "Place");
		}

		void VoxelEditor::DeleteCube() {
			DocumentCommand command(*this);
			DoPick();
			if (pickHit)
				Erase(std::vector<IntVector3>(1, PickSolid()), "Delete");
		}

		void VoxelEditor::Fill(std::vector<IntVector3> cells, std::uint32_t color,
		                       const std::string& label) {
			DocumentCommand command(*this);
			ExpandMirrors(cells); // also fill the mirror images, if enabled
			IntVector3 lo, hi;
			if (!BoundsOf(cells, lo, hi))
				return;
			// The cells may lie past the volume: grow it to hold them all, where
			// it can grow.
			const VolumeFrame frame = FrameHolding(lo, hi);
			if (!Fits(frame)) {
				SetStatus(SizeLimitMessage(frame.size));
				return;
			}
			VoxelUndoStack::Step step(undo, label);
			Reframe(frame);
			bool wrote = false;
			for (const IntVector3& c : cells) {
				const IntVector3 at = c + frame.shift;
				if (Editable(at.x, at.y, at.z) && !document.IsSolid(at.x, at.y, at.z)) {
					WriteVoxel(at.x, at.y, at.z, true, color);
					wrote = true;
				}
			}
			if (wrote)
				NoteColorUsed(color);
		}

		void VoxelEditor::Erase(std::vector<IntVector3> cells, const std::string& label) {
			DocumentCommand command(*this);
			ExpandMirrors(cells); // also erase the mirror images, if enabled
			int count = 0;
			for (const IntVector3& c : cells) {
				if (Editable(c.x, c.y, c.z) && document.IsSolid(c.x, c.y, c.z))
					count++;
			}
			if (count == 0)
				return;
			if (count > document.RemovableVoxels()) {
				SetStatus(document.RemovalLimitMessage());
				return;
			}
			VoxelUndoStack::Step step(undo, label);
			for (const IntVector3& c : cells) {
				if (Editable(c.x, c.y, c.z) && document.IsSolid(c.x, c.y, c.z))
					WriteVoxel(c.x, c.y, c.z, false, 0);
			}
			TrimVolume();
		}

		// --- Cavity ------------------------------------------------------------

		std::size_t VoxelEditor::Cell(const IntVector3& v) const {
			const IntVector3 size = document.Size();
			return std::size_t(v.x) +
			       std::size_t(size.x) * (std::size_t(v.y) + std::size_t(size.y) * std::size_t(v.z));
		}

		IntVector3 VoxelEditor::CellAt(std::size_t index) const {
			const IntVector3 size = document.Size();
			const std::size_t plane = std::size_t(size.x) * std::size_t(size.y);
			return MakeIntVector3(int(index % std::size_t(size.x)),
			                      int((index % plane) / std::size_t(size.x)), int(index / plane));
		}

		bool VoxelEditor::InVolume(const IntVector3& v) const {
			return InBounds(v.x, v.y, v.z);
		}

		std::vector<bool> VoxelEditor::OutsideAir() const {
			const IntVector3 size = document.Size();
			// A bit a cell: these maps cover the whole volume, and a model may
			// be large across even where it is shallow.
			std::vector<bool> outside(
			  std::size_t(size.x) * std::size_t(size.y) * std::size_t(size.z), false);
			std::vector<std::uint32_t> queue; // cells to flood on from, by index
			// Air at the volume's sides is outside air: past them is open space.
			auto reach = [&](const IntVector3& v) {
				if (!InVolume(v) || document.IsSolid(v.x, v.y, v.z) || outside[Cell(v)])
					return;
				outside[Cell(v)] = true;
				queue.push_back(std::uint32_t(Cell(v)));
			};
			for (int x = 0; x < size.x; x++)
				for (int y = 0; y < size.y; y++) {
					reach(MakeIntVector3(x, y, 0));
					reach(MakeIntVector3(x, y, size.z - 1));
				}
			for (int x = 0; x < size.x; x++)
				for (int z = 0; z < size.z; z++) {
					reach(MakeIntVector3(x, 0, z));
					reach(MakeIntVector3(x, size.y - 1, z));
				}
			for (int y = 0; y < size.y; y++)
				for (int z = 0; z < size.z; z++) {
					reach(MakeIntVector3(0, y, z));
					reach(MakeIntVector3(size.x - 1, y, z));
				}
			// ...and so is whatever air it reaches from there, face by face.
			for (std::size_t i = 0; i < queue.size(); i++) {
				const IntVector3 at = CellAt(queue[i]);
				for (const IntVector3& step : kFaceNeighbours)
					reach(at + step);
			}
			return outside;
		}

		void VoxelEditor::CarveCavity(int thickness) {
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			if (thickness < 1 || edit.selection.Empty()) {
				SetStatus("Nothing selected");
				return;
			}

			const std::vector<bool> outside = OutsideAir();
			auto openAt = [&](const IntVector3& v) {
				// Past the volume is open space, and so is air the outside air
				// reached. Air it did not reach is a cavity already within the
				// model, which this one may join on to.
				return !InVolume(v) || outside[Cell(v)];
			};
			auto onSkin = [&](const IntVector3& v) {
				for (const IntVector3& step : kFaceNeighbours) {
					if (openAt(v + step))
						return true;
				}
				return false;
			};

			// Every selected voxel is the mouth of the cavity, so every one of
			// them has to be a voxel, and one on the outside: a mouth facing
			// nothing but solid or another cavity opens the dig onto nothing.
			int notOnSkin = 0;
			edit.selection.ForEach([&](const IntVector3& v) {
				if (!document.IsSolid(v.x, v.y, v.z) || !onSkin(v))
					notOnSkin++;
			});
			if (notOnSkin > 0) {
				SetStatus("Cavity: " + std::to_string(notOnSkin) + " of the selected voxels " +
				          (notOnSkin == 1 ? "is" : "are") + " not on the model's outside");
				return;
			}

			// How deep each voxel lies under the outside, by faces: the wall is
			// what lies within `thickness` of it, and everything deeper is what
			// a cavity may take.
			const IntVector3 size = document.Size();
			// A byte a cell, being a count rather than a flag; the cells walked
			// are kept as indices, which are a third the size of coordinates.
			std::vector<std::uint8_t> depth(
			  std::size_t(size.x) * std::size_t(size.y) * std::size_t(size.z), 0);
			std::vector<std::uint32_t> queue; // cells to walk on from, by index
			for (int x = 0; x < size.x; x++)
				for (int y = 0; y < size.y; y++)
					for (int z = 0; z < size.z; z++) {
						const IntVector3 v = MakeIntVector3(x, y, z);
						if (!document.IsSolid(x, y, z) || !onSkin(v))
							continue;
						depth[Cell(v)] = 1;
						queue.push_back(std::uint32_t(Cell(v)));
					}
			for (std::size_t i = 0; i < queue.size(); i++) {
				const IntVector3 at = CellAt(queue[i]);
				const std::uint8_t next =
				  std::uint8_t(std::min(254, int(depth[Cell(at)]) + 1)); // deep enough is deep enough
				for (const IntVector3& step : kFaceNeighbours) {
					const IntVector3 n = at + step;
					if (!InVolume(n) || !document.IsSolid(n.x, n.y, n.z) || depth[Cell(n)] != 0)
						continue;
					depth[Cell(n)] = next;
					queue.push_back(std::uint32_t(Cell(n)));
				}
			}
			auto isWall = [&](const IntVector3& v) {
				const std::uint8_t d = depth[Cell(v)];
				return d != 0 && int(d) <= thickness;
			};

			// From the mouth inwards, as far as the solid behind it goes: what
			// the dig can reach is what it takes. It stops at the wall and never
			// steps out into the open, so a cavity stays inside the solid the
			// selection opened and the rest of the model is untouched.
			std::vector<bool> taken(depth.size(), false);
			std::vector<IntVector3> removing;
			queue.clear();
			edit.selection.ForEach([&](const IntVector3& v) {
				taken[Cell(v)] = true;
				removing.push_back(v);
				queue.push_back(std::uint32_t(Cell(v)));
			});
			const std::size_t mouth = removing.size();
			for (std::size_t i = 0; i < queue.size(); i++) {
				const IntVector3 at = CellAt(queue[i]);
				for (const IntVector3& step : kFaceNeighbours) {
					const IntVector3 n = at + step;
					if (!InVolume(n) || taken[Cell(n)] || openAt(n))
						continue;
					taken[Cell(n)] = true;
					if (!document.IsSolid(n.x, n.y, n.z)) {
						queue.push_back(std::uint32_t(Cell(n))); // a cavity already there: the dig joins it
						continue;
					}
					if (isWall(n))
						continue; // the wall is what is left standing
					removing.push_back(n);
					queue.push_back(std::uint32_t(Cell(n)));
				}
			}
			if (removing.size() == mouth) {
				SetStatus("Cavity: nothing behind the selection to hollow out");
				return;
			}

			// The voxels this document will not have edited are left where they
			// are, as every other edit leaves them.
			removing.erase(std::remove_if(removing.begin(), removing.end(),
			                              [&](const IntVector3& v) {
				                              return !Editable(v.x, v.y, v.z);
			                              }),
			               removing.end());
			if (int(removing.size()) > document.RemovableVoxels()) {
				SetStatus(document.RemovalLimitMessage());
				return;
			}

			{
				VoxelUndoStack::Step step(undo, "Cavity");
				edit.selection.Clear(); // the voxels it held are the mouth, now gone
				for (const IntVector3& v : removing)
					WriteVoxel(v.x, v.y, v.z, false, 0);
				TrimVolume();
			}
			SetStatus("Cavity: removed " + std::to_string(removing.size()) + " voxels, wall " +
			          std::to_string(thickness) + (thickness == 1 ? " voxel" : " voxels") + " thick");
		}

		// --- Sweeping (Extrude, Pocket) ----------------------------------------

		std::vector<VoxelEditor::SweptCell> VoxelEditor::SweepPath(int axis, int steps, bool pocket,
		                                                           bool mirrored) const {
			std::vector<SweptCell> path;
			if (axis < 0 || axis > 2 || steps == 0)
				return path;
			IntVector3 unit = MakeIntVector3(0, 0, 0);
			(axis == 0 ? unit.x : axis == 1 ? unit.y : unit.z) = steps > 0 ? 1 : -1;
			const int count = std::abs(steps);
			// An extrusion starts past each voxel; a pocket digs the voxel too.
			const int first = pocket ? 0 : 1;
			const int last = pocket ? count - 1 : count;
			edit.selection.ForEach([&](const IntVector3& v) {
				const std::uint32_t color = document.Color(v.x, v.y, v.z);
				for (int k = first; k <= last; k++) {
					std::vector<IntVector3> cells{v + unit * k};
					if (mirrored)
						ExpandMirrors(cells);
					for (const IntVector3& c : cells)
						path.push_back(SweptCell{c, color});
				}
			});
			// Several voxels sweep through the same cell: it changes once, in
			// the colour of the first to reach it.
			std::stable_sort(path.begin(), path.end(), [](const SweptCell& a, const SweptCell& b) {
				return std::tie(a.at.x, a.at.y, a.at.z) < std::tie(b.at.x, b.at.y, b.at.z);
			});
			path.erase(std::unique(path.begin(), path.end(),
			                       [](const SweptCell& a, const SweptCell& b) { return a.at == b.at; }),
			           path.end());
			return path;
		}

		std::vector<IntVector3> VoxelEditor::SweptCells(int axis, int steps, bool pocket) const {
			std::vector<IntVector3> cells;
			for (const SweptCell& c : SweepPath(axis, steps, pocket, false)) {
				const IntVector3& at = c.at;
				const bool solid = InBounds(at.x, at.y, at.z) && document.IsSolid(at.x, at.y, at.z);
				// A pocket digs what is there; an extrusion fills what is not.
				if (pocket ? (solid && Editable(at.x, at.y, at.z)) : !solid)
					cells.push_back(at);
			}
			return cells;
		}

		void VoxelEditor::SweepSelection(int axis, int steps, bool pocket) {
			if (axis < 0 || axis > 2 || steps == 0)
				return;
			DocumentCommand command(*this);
			if (edit.selection.Empty())
				return;
			IntVector3 reach = MakeIntVector3(0, 0, 0);
			(axis == 0 ? reach.x : axis == 1 ? reach.y : reach.z) = steps;
			std::vector<IntVector3> selected;
			edit.selection.ForEach([&](const IntVector3& v) { selected.push_back(v); });
			const std::vector<SweptCell> path = SweepPath(axis, steps, pocket, true);

			if (pocket) {
				int count = 0;
				for (const SweptCell& c : path) {
					if (Editable(c.at.x, c.at.y, c.at.z) && document.IsSolid(c.at.x, c.at.y, c.at.z))
						count++;
				}
				if (count == 0) {
					SetStatus("Nothing to dig there");
					return;
				}
				if (count > document.RemovableVoxels()) {
					SetStatus(document.RemovalLimitMessage());
					return;
				}
				VoxelUndoStack::Step step(undo, "Pocket");
				for (const SweptCell& c : path) {
					if (Editable(c.at.x, c.at.y, c.at.z) && document.IsSolid(c.at.x, c.at.y, c.at.z))
						WriteVoxel(c.at.x, c.at.y, c.at.z, false, 0);
				}
				// The pockets' floors are selected, so the next sweep digs on.
				edit.selection.Clear();
				for (const IntVector3& v : selected)
					SelectIfSolid(v + reach);
				TrimVolume();
				return;
			}

			std::vector<IntVector3> cells;
			for (const SweptCell& c : path)
				cells.push_back(c.at);
			IntVector3 lo, hi;
			if (!BoundsOf(cells, lo, hi))
				return;
			const VolumeFrame frame = FrameHolding(lo, hi);
			if (!Fits(frame)) {
				SetStatus(SizeLimitMessage(frame.size));
				return;
			}
			VoxelUndoStack::Step step(undo, "Extrude");
			Reframe(frame);
			bool wrote = false;
			for (const SweptCell& c : path) {
				const IntVector3 at = c.at + frame.shift;
				if (Editable(at.x, at.y, at.z) && !document.IsSolid(at.x, at.y, at.z)) {
					WriteVoxel(at.x, at.y, at.z, true, c.color);
					wrote = true;
				}
			}
			if (!wrote)
				SetStatus("Nothing to extrude: the way is filled");
			// The ends of the extrusion are selected, so the next sweep goes on
			// from them.
			edit.selection.Clear();
			for (const IntVector3& v : selected)
				SelectIfSolid(v + reach + frame.shift);
		}

		void VoxelEditor::FillCells(const std::vector<IntVector3>& cells, std::uint32_t color) {
			Fill(cells, color, "Fill");
		}

		void VoxelEditor::EraseCells(const std::vector<IntVector3>& cells) {
			Erase(cells, "Erase");
		}

		void VoxelEditor::PaintCells(const std::vector<IntVector3>& cellsIn, std::uint32_t color) {
			DocumentCommand command(*this);
			std::vector<IntVector3> cells = cellsIn;
			ExpandMirrors(cells); // also recolour the mirror images, if enabled
			std::uint32_t rgb = color & 0xFFFFFF;
			VoxelUndoStack::Step step(undo, "Paint");
			bool wrote = false;
			for (const IntVector3& c : cells) {
				if (!Editable(c.x, c.y, c.z) || !document.IsSolid(c.x, c.y, c.z))
					continue; // paint only existing voxels; never grows the volume
				if (document.Color(c.x, c.y, c.z) != rgb) {
					WriteVoxel(c.x, c.y, c.z, true, rgb);
					wrote = true;
				}
			}
			if (wrote)
				NoteColorUsed(rgb);
		}

		// --- Colour -----------------------------------------------------------

		Vector4 VoxelEditor::ColorToVec(std::uint32_t c) const {
			return ConvertColorRGBA(IntVectorFromColor(c));
		}

		bool VoxelEditor::SamplingArmed() const {
			// A colour is sampled from the document, which only Edit mode shows.
			if (currentMode != EditorMode::Edit)
				return false;
			return altHeld || ui.GetColorPicker()->GetEyedropperMode();
		}

		bool VoxelEditor::SampleColor() {
			DoPick();
			if (!pickHit)
				return false;
			// Into whatever the picker is setting: the brush, or the colour a
			// tool has it set instead.
			std::uint32_t& target = PickerColor();
			target = document.Color(pickSolid.x, pickSolid.y, pickSolid.z);
			ColorPicker& picker = *ui.GetColorPicker();
			picker.SetColor(target);
			picker.SetEyedropperMode(false); // a pick is one-shot
			SetStatus("Picked colour");
			return true;
		}

		void VoxelEditor::NoteColorUsed(std::uint32_t color) {
			ui.GetColorPicker()->AddRecentColor(color);
		}

		void VoxelEditor::NoteDocumentColors() {
			// Only voxels with a face open to the air count: those inside are
			// never seen, and a solid interior would outweigh every colour
			// that is.
			const IntVector3 size = document.Size();
			std::unordered_map<std::uint32_t, int> uses;
			for (int x = 0; x < size.x; x++)
				for (int y = 0; y < size.y; y++)
					for (int z = 0; z < size.z; z++) {
						if (!document.IsSolid(x, y, z))
							continue;
						bool exposed = false;
						for (const IntVector3& step : kFaceNeighbours) {
							const IntVector3 n = MakeIntVector3(x, y, z) + step;
							if (!InBounds(n.x, n.y, n.z) || !document.IsSolid(n.x, n.y, n.z)) {
								exposed = true;
								break;
							}
						}
						if (exposed)
							uses[document.Color(x, y, z) & 0xFFFFFF]++;
					}

			std::vector<std::pair<std::uint32_t, int>> ranked(uses.begin(), uses.end());
			const std::size_t kept =
			  std::min(ranked.size(), std::size_t(ColorPicker::kRecentSlots));
			// Most used first; equal counts in colour order, so the row is the
			// same every time the model is opened.
			std::partial_sort(ranked.begin(), ranked.begin() + std::ptrdiff_t(kept), ranked.end(),
			                  [](const std::pair<std::uint32_t, int>& a,
			                     const std::pair<std::uint32_t, int>& b) {
				                  return a.second != b.second ? a.second > b.second
				                                              : a.first < b.first;
			                  });
			// Each colour noted goes first in the row, so the most used goes last.
			for (std::size_t i = kept; i-- > 0;)
				NoteColorUsed(ranked[i].first);
		}

		// --- Selection --------------------------------------------------------

		bool VoxelEditor::IsSelected(int x, int y, int z) const {
			return edit.selection.Contains(MakeIntVector3(x, y, z));
		}

		bool VoxelEditor::SelectionBounds(IntVector3& lo, IntVector3& hi) const {
			if (edit.placing) {
				lo = edit.placement.anchor;
				hi = lo + edit.placement.Extent() - MakeIntVector3(1, 1, 1);
				return true;
			}
			return edit.selection.Bounds(lo, hi);
		}

		int VoxelEditor::SelectionCount() const {
			// While a paste or an import waits it is what the selection commands
			// act on.
			return edit.placing ? int(edit.placement.voxels->size()) : edit.selection.Size();
		}

		void VoxelEditor::ClearSelection() {
			const int count = SelectionCount();
			{
				DocumentCommand command(*this);
				VoxelUndoStack::Step step(undo, "Clear Selection");
				edit.selection.Clear();
			}
			// Reported here rather than by whoever asked: a command that seems to
			// do nothing reads as broken, and it is asked for from the menu, a
			// key and a button alike.
			SetStatus(count ? "Deselected " + std::to_string(count) + " voxels"
			                : "Nothing was selected");
		}

		std::vector<IntVector3> VoxelEditor::LinkedColorRegion(int x, int y, int z) const {
			std::vector<IntVector3> region;
			if (!document.IsSolid(x, y, z))
				return region;
			const std::uint32_t target = document.Color(x, y, z);
			auto linked = [&](const IntVector3& v) {
				return document.IsSolid(v.x, v.y, v.z) && document.Color(v.x, v.y, v.z) == target;
			};
			// Every cell is queued once: `queued` holds all that ever were.
			VoxelSelection queued;
			std::vector<IntVector3> stack(1, MakeIntVector3(x, y, z));
			queued.Add(stack.back());
			while (!stack.empty()) {
				const IntVector3 c = stack.back();
				stack.pop_back();
				region.push_back(c);
				for (const IntVector3& step : kFaceNeighbours) {
					const IntVector3 n = c + step;
					if (!queued.Contains(n) && linked(n)) {
						queued.Add(n);
						stack.push_back(n);
					}
				}
			}
			return region;
		}

		std::vector<IntVector3> VoxelEditor::CoplanarFaces(const IntVector3& solid,
		                                                   const IntVector3& normal) const {
			std::vector<IntVector3> region;
			auto showing = [&](const IntVector3& v) {
				const IntVector3 out = v + normal;
				return document.IsSolid(v.x, v.y, v.z) && !document.IsSolid(out.x, out.y, out.z);
			};
			if (!showing(solid))
				return region;
			// Across the plane only: the four face steps square to the normal.
			std::vector<IntVector3> steps;
			for (const IntVector3& step : kFaceNeighbours) {
				if (step.x * normal.x + step.y * normal.y + step.z * normal.z == 0)
					steps.push_back(step);
			}
			VoxelSelection queued;
			std::vector<IntVector3> stack(1, solid);
			queued.Add(solid);
			while (!stack.empty()) {
				const IntVector3 c = stack.back();
				stack.pop_back();
				region.push_back(c);
				for (const IntVector3& step : steps) {
					const IntVector3 n = c + step;
					if (!queued.Contains(n) && showing(n)) {
						queued.Add(n);
						stack.push_back(n);
					}
				}
			}
			return region;
		}

		void VoxelEditor::IndexCells() const {
			if (cellsVersion == document.Version())
				return;
			cellsVersion = document.Version();
			solidCells.clear();
			surfaceCells.clear();
			const IntVector3 size = document.Size();
			for (int x = 0; x < size.x; x++)
				for (int y = 0; y < size.y; y++)
					for (int z = 0; z < size.z; z++) {
						if (!document.IsSolid(x, y, z))
							continue;
						const IntVector3 v = MakeIntVector3(x, y, z);
						solidCells.push_back(v);
						for (const IntVector3& step : kFaceNeighbours) {
							const IntVector3 n = v + step;
							if (!document.IsSolid(n.x, n.y, n.z)) {
								surfaceCells.push_back(v);
								break;
							}
						}
					}
		}

		bool VoxelEditor::Seen(const IntVector3& v, const GizmoView& view) const {
			// Through the middle of each face that shows and faces the eye, just
			// inside it, so the ray meets this voxel unless another is in the way.
			const Vector3 middle = MakeVector3(float(v.x), float(v.y), float(v.z));
			for (const IntVector3& step : kFaceNeighbours) {
				const IntVector3 n = v + step;
				if (document.IsSolid(n.x, n.y, n.z))
					continue; // covered
				const Vector3 out = MakeVector3(float(step.x), float(step.y), float(step.z));
				const Vector3 face = middle + out * 0.49F;
				if (Vector3::Dot(out, view.eye - face) <= 0.0F)
					continue; // turned away
				Vector3 dir = face - view.eye;
				const float length = dir.GetLength();
				if (length < 1.0e-4F)
					continue;
				IntVector3 hit, before;
				if (FirstSolidAlong(view.eye, dir / length, hit, before) && hit == v)
					return true;
			}
			return false;
		}

		std::vector<IntVector3> VoxelEditor::CellsOnScreen(
		  const std::function<bool(const Vector2&)>& inside, bool through) const {
			std::vector<IntVector3> found;
			const GizmoView view = DocumentView();
			if (!view.IsValid())
				return found;
			IndexCells();
			// Through the volume, every voxel; else only those with a face
			// showing can be seen, and of them, those that are.
			for (const IntVector3& v : through ? solidCells : surfaceCells) {
				Vector2 screen;
				if (!view.Project(MakeVector3(float(v.x), float(v.y), float(v.z)), screen) ||
				    !inside(screen))
					continue;
				if (through || Seen(v, view))
					found.push_back(v);
			}
			return found;
		}

		void VoxelEditor::SelectIfSolid(const IntVector3& v) {
			if (Editable(v.x, v.y, v.z) && document.IsSolid(v.x, v.y, v.z))
				edit.selection.Add(v);
		}

		void VoxelEditor::SelectBox(const IntVector3& lo, const IntVector3& hi) {
			DocumentCommand command(*this);
			VoxelUndoStack::Step step(undo, "Select");
			for (int x = lo.x; x <= hi.x; x++)
				for (int y = lo.y; y <= hi.y; y++)
					for (int z = lo.z; z <= hi.z; z++)
						SelectIfSolid(MakeIntVector3(x, y, z));
		}

		void VoxelEditor::SelectAll() {
			SelectBox(MakeIntVector3(0, 0, 0), document.Size() - MakeIntVector3(1, 1, 1));
			SetStatus("Selected " + std::to_string(SelectionCount()) + " voxels");
		}

		void VoxelEditor::InvertSelection() {
			{
				DocumentCommand command(*this);
				VoxelUndoStack::Step step(undo, "Invert Selection");
				// Built aside: what is selected now is what is being read.
				const VoxelSelection before = edit.selection;
				edit.selection.Clear();
				const IntVector3 size = document.Size();
				for (int x = 0; x < size.x; x++)
					for (int y = 0; y < size.y; y++)
						for (int z = 0; z < size.z; z++) {
							const IntVector3 v = MakeIntVector3(x, y, z);
							if (!before.Contains(v))
								SelectIfSolid(v);
						}
			}
			SetStatus("Selected " + std::to_string(SelectionCount()) + " voxels");
		}

		void VoxelEditor::SelectCells(const std::vector<IntVector3>& cells) {
			DocumentCommand command(*this);
			VoxelUndoStack::Step step(undo, "Select");
			for (const IntVector3& c : cells)
				SelectIfSolid(c);
		}

		void VoxelEditor::DeselectCells(const std::vector<IntVector3>& cells) {
			DocumentCommand command(*this);
			VoxelUndoStack::Step step(undo, "Deselect");
			for (const IntVector3& c : cells)
				edit.selection.Remove(c);
		}

		// --- Clipboard / paste -----------------------------------------------
		//
		// Copy, Cut and Delete act on the selection. While a paste or an import
		// waits, it is what the user is handling, so the commands act on it
		// where it is, and never place it first.

		void VoxelEditor::CopySelection() {
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			std::vector<ClipVoxel> copied;
			IntVector3 lo, hi;
			if (edit.placing) {
				copied = *edit.placement.voxels; // already relative to their box
			} else if (edit.selection.Bounds(lo, hi)) {
				copied.reserve(size_t(edit.selection.Size()));
				edit.selection.ForEach([&](const IntVector3& v) {
					copied.push_back({v - lo, document.Color(v.x, v.y, v.z)});
				});
			}
			if (copied.empty()) {
				SetStatus("Nothing selected");
				return;
			}
			clipboard = std::move(copied);
			SetStatus("Copied " + std::to_string(clipboard.size()) + " voxels");
		}

		bool VoxelEditor::CutSelection() {
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			if (!CanEraseSelection())
				return false;
			CopySelection();
			SetStatus("Cut " + std::to_string(EraseSelection("Cut")) + " voxels");
			return true;
		}

		void VoxelEditor::DeleteSelection() {
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			if (!CanEraseSelection())
				return;
			SetStatus("Deleted " + std::to_string(EraseSelection("Delete")) + " voxels");
		}

		void VoxelEditor::RecolorSelection(std::uint32_t color) {
			// Pending voxels are recoloured where they wait, not placed first.
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			if (SelectionCount() == 0) {
				SetStatus("Nothing selected");
				return;
			}
			const std::uint32_t rgb = color & 0xFFFFFF;
			VoxelUndoStack::Step step(undo, "Recolour");
			int recolored = 0;
			if (edit.placing) {
				bool differs = false;
				for (const ClipVoxel& v : *edit.placement.voxels)
					differs = differs || v.color != rgb;
				// Editing the shared list would copy it even for no change.
				if (differs) {
					for (ClipVoxel& v : edit.placement.voxels.Edit()) {
						if (v.color != rgb) {
							v.color = rgb;
							recolored++;
						}
					}
				}
			} else {
				// A copy to walk: the selection is shared until changed, so it costs
				// nothing, and writing voxels never touches what it is walking.
				const VoxelSelection selected = edit.selection;
				selected.ForEach([&](const IntVector3& v) {
					if (!Editable(v.x, v.y, v.z) || !document.IsSolid(v.x, v.y, v.z) ||
					    document.Color(v.x, v.y, v.z) == rgb)
						return;
					WriteVoxel(v.x, v.y, v.z, true, rgb);
					recolored++;
				});
			}
			if (recolored > 0)
				NoteColorUsed(rgb);
			SetStatus("Recoloured " + std::to_string(recolored) + " voxels");
		}

		// Live, non-journaled recolour for a colour still being chosen; the tool
		// commits the final one with RecolorSelection, which puts this back first.
		void VoxelEditor::PreviewRecolorSelection(std::uint32_t color) {
			if (SelectionCount() == 0)
				return;
			const std::uint32_t rgb = color & 0xFFFFFF;
			if (edit.placing) {
				if (!previewedPlacement)
					previewedPlacement = edit.placement; // shares the voxels until edited
				for (ClipVoxel& v : edit.placement.voxels.Edit())
					v.color = rgb;
				return;
			}
			if (!previewedColors) {
				previewedColors.emplace();
				edit.selection.ForEach([&](const IntVector3& v) {
					if (Editable(v.x, v.y, v.z) && document.IsSolid(v.x, v.y, v.z))
						previewedColors->emplace_back(v, document.Color(v.x, v.y, v.z));
				});
			}
			for (const auto& voxel : *previewedColors) {
				const IntVector3& v = voxel.first;
				document.Write(v.x, v.y, v.z, true, rgb);
			}
		}

		bool VoxelEditor::CanEraseSelection() {
			const int count =
			  edit.placing ? int(edit.placement.voxels->size()) : edit.selection.Size();
			if (count == 0) {
				SetStatus("Nothing selected");
				return false;
			}
			// Pending voxels are out of the document already: dropping them
			// leaves what it holds now.
			if (!edit.placing && count > document.RemovableVoxels()) {
				SetStatus(document.RemovalLimitMessage());
				return false;
			}
			return true;
		}

		int VoxelEditor::EraseSelection(const std::string& label) {
			VoxelUndoStack::Step step(undo, label);
			if (edit.placing) {
				// A paste or an import vanishes; the document never held it.
				const int erased = int(edit.placement.voxels->size());
				DropPlacement();
				TrimVolume();
				return erased;
			}
			const VoxelSelection selected = edit.selection;
			edit.selection.Clear();
			selected.ForEach([&](const IntVector3& v) { WriteVoxel(v.x, v.y, v.z, false, 0); });
			TrimVolume();
			return selected.Size();
		}

		void VoxelEditor::Paste() {
			if (clipboard.empty()) {
				SetStatus("Clipboard is empty");
				return;
			}
			// Pending voxels land first, and landing them reframes the volume: the
			// middle is read once the document is the one the paste goes into.
			DocumentCommand command(*this);
			const IntVector3 size = document.Size();
			IntVector3 anchor = MakeIntVector3(size.x / 2, size.y / 2, size.z / 2);
			if (placeAtAim)
				AimedAnchor(ExtentOf(clipboard), anchor);
			StartPlacement(clipboard, "Paste", anchor);
		}

		bool VoxelEditor::AimedAnchor(const IntVector3& extent, IntVector3& anchor) {
			DoPick();
			if (!pickHit)
				return false;
			// Centred across the cell before the hit, the box's near side on it.
			const IntVector3 at = pickPlace;
			anchor = MakeIntVector3(at.x - extent.x / 2, at.y - extent.y / 2, at.z - extent.z / 2);
			const IntVector3 face = pickPlace - pickSolid;
			if (face.x != 0)
				anchor.x = face.x > 0 ? at.x : at.x - extent.x + 1;
			if (face.y != 0)
				anchor.y = face.y > 0 ? at.y : at.y - extent.y + 1;
			if (face.z != 0)
				anchor.z = face.z > 0 ? at.z : at.z - extent.z + 1;
			return true;
		}

		void VoxelEditor::StartPlacement(std::vector<ClipVoxel> voxels, const std::string& label,
		                                 const IntVector3& anchor) {
			if (voxels.empty())
				return;
			// Anything already pending belongs to the document before this starts.
			DocumentCommand command(*this);

			IntVector3 at = anchor;
			if (!ClampPlacementAnchor(ExtentOf(voxels), at)) {
				SetStatus(label + ": too large for the maximum " + document.Noun() + " size");
				return;
			}
			{
				VoxelUndoStack::Step step(undo, label);
				edit.placement = MakePlacement(std::move(voxels), at, label);
				edit.placing = true;
			}

			// Positioning a placement is what the Transform tool is for, so go there.
			if (!ActivateTransformTool()) {
				SetStatus(label + ": could not open the Transform tool");
				return;
			}
			SetStatus(label + ": position it with the gizmo, then click away to place it");
		}

		bool VoxelEditor::ActivateTransformTool() {
			// Matched by type rather than by label, so neither renaming a button nor
			// another sub-tool taking the same label can send a placement elsewhere.
			for (size_t i = 0; i < tools.size(); i++) {
				VoxelTool& tool = *tools[i].tool;
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

		void VoxelEditor::InsertModel(const VoxelModel& imported) {
			// Pending voxels land first, and landing them moves the pivot the
			// import is aligned on: it is read once that has happened.
			DocumentCommand command(*this);

			// Collect the solid voxels relative to their own min corner. Interior
			// voxels keep the sentinel colour the loader fills them with, exactly as
			// in a document opened from disk; the writer drops them again on save.
			int minX = imported.GetWidth(), minY = imported.GetHeight(),
			    minZ = imported.GetDepth();
			int maxX = -1, maxY = -1, maxZ = -1;
			for (int x = 0; x < imported.GetWidth(); x++)
				for (int y = 0; y < imported.GetHeight(); y++)
					for (int z = 0; z < imported.GetDepth(); z++) {
						if (!imported.IsSolid(x, y, z))
							continue;
						minX = std::min(minX, x);
						maxX = std::max(maxX, x);
						minY = std::min(minY, y);
						maxY = std::max(maxY, y);
						minZ = std::min(minZ, z);
						maxZ = std::max(maxZ, z);
					}
			if (maxX < 0) {
				SetStatus("That model is empty");
				return;
			}

			std::vector<ClipVoxel> voxels;
			for (int x = minX; x <= maxX; x++)
				for (int y = minY; y <= maxY; y++)
					for (int z = minZ; z <= maxZ; z++) {
						if (imported.IsSolid(x, y, z))
							voxels.push_back({MakeIntVector3(x - minX, y - minY, z - minZ),
							                  imported.GetColor(x, y, z) & 0xFFFFFF});
					}

			// Start with the imported pivot on this document's pivot, which puts a
			// part (a hand, a barrel) where it belongs before any dragging.
			Vector3 importedPivot = imported.GetOrigin() * -1.0F;
			Vector3 aligned =
			  MakeVector3(float(minX), float(minY), float(minZ)) + GetPivot() - importedPivot;
			IntVector3 anchor = MakeIntVector3(int(std::floor(aligned.x + 0.5F)),
			                                   int(std::floor(aligned.y + 0.5F)),
			                                   int(std::floor(aligned.z + 0.5F)));
			if (placeAtAim)
				AimedAnchor(ExtentOf(voxels), anchor);
			StartPlacement(std::move(voxels), "Insert", anchor);
		}

		bool VoxelEditor::PlacementFromSelection(PendingPlacement& out) const {
			IntVector3 lo, hi, middle;
			if (!edit.selection.Bounds(lo, hi) || !SelectionMiddleOf(middle))
				return false;
			std::vector<ClipVoxel> voxels;
			voxels.reserve(size_t(edit.selection.Size()));
			edit.selection.ForEach([&](const IntVector3& v) {
				voxels.push_back({v - lo, document.Color(v.x, v.y, v.z)});
			});
			out = MakePlacement(std::move(voxels), lo, "Transform");
			out.pivot = middle;
			return true;
		}

		bool VoxelEditor::SelectionMiddleOf(IntVector3& out) const {
			if (movedMiddle.valid && movedMiddle.selectionVersion == edit.selection.Version() &&
			    !edit.selection.Empty()) {
				out = movedMiddle.at;
				return true;
			}
			IntVector3 lo, hi;
			if (!edit.selection.Bounds(lo, hi))
				return false;
			out = MiddleOf(lo, hi);
			return true;
		}

		void VoxelEditor::MoveSelection(const PlacementTransform& t) {
			PendingPlacement selected;
			if (!PlacementFromSelection(selected))
				return;
			PendingPlacement moved;
			bool clamped = false;
			if (!TransformedPlacement(selected, t, moved, clamped)) {
				SetStatus(SizeLimitMessage(moved.Extent()));
				return;
			}
			if (clamped)
				SetStatus(SizeLimitMessage()); // went as far as the limit allows
			// Where the moved voxels land must fit the volume they leave behind,
			// grown to hold them; with every voxel moving, nothing else holds it.
			const VolumeFrame frame =
			  FrameHolding(moved.anchor, moved.anchor + moved.Extent() - MakeIntVector3(1, 1, 1));
			if (!Fits(frame)) {
				SetStatus(SizeLimitMessage(frame.size));
				return;
			}

			VoxelUndoStack::Step step(undo, t.Turns() ? "Turn" : "Move");
			// They leave first, so a voxel landing where another of them was is
			// written, not wiped out by the one that left.
			const VoxelSelection leaving = edit.selection;
			edit.selection.Clear();
			leaving.ForEach([&](const IntVector3& v) { WriteVoxel(v.x, v.y, v.z, false, 0); });
			LandGroup(moved);
			// The middle stays with the voxels: shifted by the reframe, and by
			// the trim that follows, along with the selection it belongs to.
			movedMiddle.valid = true;
			movedMiddle.selectionVersion = edit.selection.Version();
			movedMiddle.at = moved.pivot + frame.shift;
			TrimVolume();
		}

		int VoxelEditor::LandGroup(const PendingPlacement& group) {
			const VolumeFrame frame =
			  FrameHolding(group.anchor, group.anchor + group.Extent() - MakeIntVector3(1, 1, 1));
			Reframe(frame);
			edit.selection.Clear(); // what lands is what is selected
			int landed = 0;
			for (const ClipVoxel& v : *group.voxels) {
				if (LandVoxel(group.anchor + v.rel + frame.shift, v.color))
					landed++;
			}
			return landed;
		}

		bool VoxelEditor::LandVoxel(const IntVector3& at, std::uint32_t color) {
			if (!Editable(at.x, at.y, at.z))
				return false;
			WriteVoxel(at.x, at.y, at.z, true, color);
			edit.selection.Add(at);
			return true;
		}

		VoxelEditor::DocumentCommand::DocumentCommand(VoxelEditor& editor, Pending pending)
		    : editor(editor) {
			// Counted only once the preparation is done: were it to throw, this
			// scope never existed and later commands must still see depth 0.
			if (editor.documentCommandDepth == 0) {
				editor.EndPreviews();
				if (pending == Pending::Apply)
					editor.ApplyPlacement();
			}
			editor.documentCommandDepth++;
		}

		VoxelEditor::DocumentCommand::~DocumentCommand() {
			if (--editor.documentCommandDepth > 0)
				return;
			// Whatever the command got done, even if it threw part way, the tool
			// catches up with it. Nothing may leave a destructor, so a tool that
			// fails to is logged instead.
			try {
				editor.NotifyDocumentChanged();
			} catch (const std::exception& ex) {
				SPLog("Editor tool failed to follow a document change: %s", ex.what());
			} catch (...) {
				SPLog("Editor tool failed to follow a document change");
			}
		}

		void VoxelEditor::DropPlacement() {
			edit.placing = false;
			edit.placement = PendingPlacement();
		}

		PendingPlacement VoxelEditor::MakePlacement(std::vector<ClipVoxel> voxels,
		                                            const IntVector3& anchor,
		                                            const std::string& label) {
			PendingPlacement p;
			p.voxels = CopyOnWrite<std::vector<ClipVoxel>>(std::move(voxels));
			p.anchor = anchor;
			// A turn about their middle keeps them about where they were.
			p.pivot = MiddleOf(anchor, anchor + p.Extent() - MakeIntVector3(1, 1, 1));
			p.label = label;
			return p;
		}

		bool VoxelEditor::TransformedPlacement(const PendingPlacement& from,
		                                       const PlacementTransform& t, PendingPlacement& out,
		                                       bool& clamped) const {
			out = from;
			clamped = false;

			if (t.Turns()) {
				// A quarter turn takes the axis after `t.axis` onto the one after
				// that, which is the right-handed sense about `t.axis`. Offsets from
				// a whole-voxel centre stay whole, so every voxel lands on a voxel.
				const int turns = ((t.quarterTurns % 4) + 4) % 4;
				const int u = (t.axis + 1) % 3, v = (t.axis + 2) % 3;
				const IntVector3 centre = TurnCentre(from.pivot);
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
				// A turn is the one change that rewrites the voxels themselves.
				std::vector<ClipVoxel>& voxels = out.voxels.Edit();
				std::vector<IntVector3> cells(voxels.size());
				for (size_t i = 0; i < voxels.size(); i++)
					cells[i] = turned(from.anchor + voxels[i].rel);
				IntVector3 lo, hi;
				BoundsOf(cells, lo, hi);
				out.anchor = lo;
				for (size_t i = 0; i < voxels.size(); i++)
					voxels[i].rel = cells[i] - lo;
				// Their middle turns with them, so it is still their middle
				// whichever centre they turned about.
				out.pivot = turned(from.pivot);
			}

			out.anchor = out.anchor + t.shift;
			out.pivot = out.pivot + t.shift;
			IntVector3 fitted = out.anchor;
			if (!ClampPlacementAnchor(out.Extent(), fitted))
				return false;
			clamped = !(fitted == out.anchor);
			// The pivot stays with the voxels, so turning back still returns them.
			out.pivot = out.pivot + (fitted - out.anchor);
			out.anchor = fitted;
			return true;
		}

		void VoxelEditor::TransformPlacement(const PlacementTransform& t) {
			if (!t.IsValid() || t.IsIdentity())
				return;
			if (!edit.placing) {
				// The selection moves in the document itself.
				DocumentCommand command(*this);
				MoveSelection(t);
				return;
			}
			PendingPlacement next;
			bool clamped = false;
			if (!TransformedPlacement(edit.placement, t, next, clamped)) {
				SetStatus(SizeLimitMessage(next.Extent()));
				return;
			}
			if (clamped)
				SetStatus(SizeLimitMessage()); // went as far as the limit allows
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			VoxelUndoStack::Step step(undo, t.Turns() ? "Turn" : "Move");
			edit.placement = std::move(next);
		}

		bool VoxelEditor::MovedMiddle(IntVector3& out) const {
			// From what is known already (the waiting voxels' middle, the
			// selection's cached box): it is asked for every frame.
			if (edit.placing) {
				out = edit.placement.pivot;
				return true;
			}
			return SelectionMiddleOf(out);
		}

		bool VoxelEditor::TransformPivot(IntVector3& out) const {
			IntVector3 middle;
			if (!MovedMiddle(middle))
				return false;
			out = TurnCentre(middle);
			return true;
		}

		IntVector3 VoxelEditor::PivotVoxel() const {
			// The voxel nearest the pivot; on a tie the lower one, as for the
			// middle of a group.
			const Vector3 p = GetPivot();
			auto nearest = [](float c) { return int(std::ceil(c - 0.5F)); };
			return MakeIntVector3(nearest(p.x), nearest(p.y), nearest(p.z));
		}

		IntVector3 VoxelEditor::TurnCentre(const IntVector3& groupMiddle) const {
			return turnsAboutModelPivot ? PivotVoxel() : groupMiddle;
		}

		bool VoxelEditor::ClampPlacementAnchor(const IntVector3& box, IntVector3& anchor) const {
			const IntVector3 extentNow = document.Size();
			const IntVector3 limitNow = document.MaxSize();
			const int extent[3] = {box.x, box.y, box.z};
			const int size[3] = {extentNow.x, extentNow.y, extentNow.z};
			const int limit[3] = {limitNow.x, limitNow.y, limitNow.z};
			int at[3] = {anchor.x, anchor.y, anchor.z};
			for (int a = 0; a < 3; a++) {
				if (extent[a] > limit[a] || size[a] > limit[a])
					return false;
				// Landing grows the volume to span min(0, at) .. max(size, at + extent),
				// which stays within the limit exactly for `at` in this range. A
				// volume that cannot grow has its size as its limit, which keeps
				// what lands inside it.
				at[a] = std::max(size[a] - limit[a], std::min(limit[a] - extent[a], at[a]));
			}
			anchor = MakeIntVector3(at[0], at[1], at[2]);
			return true;
		}

		void VoxelEditor::ApplyPlacement() {
			if (!edit.placing)
				return;
			const PendingPlacement pending = edit.placement;

			// Every placement is kept where it fits (ClampPlacementAnchor), so the
			// volume holding it does; were it ever not to, dropping the voxels
			// loses nothing of the document.
			const VolumeFrame frame = FrameHolding(
			  pending.anchor, pending.anchor + pending.Extent() - MakeIntVector3(1, 1, 1));
			if (!Fits(frame)) {
				CancelPlacement();
				SetStatus(SizeLimitMessage(frame.size));
				return;
			}

			int placed = 0;
			{
				VoxelUndoStack::Step step(undo, pending.label);
				// No longer pending before the volume changes, so the relabelling
				// leaves `pending` in the frame it was measured in.
				DropPlacement();
				placed = LandGroup(pending);
				TrimVolume();
			}
			SetStatus(pending.label + ": placed " + std::to_string(placed) + " voxels");
		}

		void VoxelEditor::CancelPlacement() {
			if (!edit.placing)
				return;
			const std::string label = edit.placement.label;
			{
				VoxelUndoStack::Step step(undo, "Cancel " + label);
				DropPlacement();
			}
			SetStatus(label + " cancelled");
		}

		void VoxelEditor::DrawPlacementPreview() {
			// The voxels themselves are rendered solid (see DrawScene); outline the
			// volume so it reads as pending rather than part of the document.
			if (!edit.placing)
				return;
			const PendingPlacement& pending = edit.placement;
			DrawBoxOutline(pending.anchor, pending.anchor + pending.Extent() - MakeIntVector3(1, 1, 1),
			               MakeVector4(0.4F, 1.0F, 0.5F, 0.9F));
		}

		void VoxelEditor::DrawPlacementTransformed(const PlacementTransform& t,
		                                           const Vector4& color) {
			if (!t.IsValid())
				return;
			PendingPlacement selected;
			if (!edit.placing && !PlacementFromSelection(selected))
				return;
			PendingPlacement preview;
			bool clamped = false;
			if (!TransformedPlacement(edit.placing ? edit.placement : selected, t, preview, clamped))
				return;
			for (const ClipVoxel& v : *preview.voxels) {
				const IntVector3 at = preview.anchor + v.rel;
				DrawCellOutline(at.x, at.y, at.z, color);
			}
		}

		void VoxelEditor::RefreshPlacementModel() {
			// The pending voxels' model follows their voxels' version, so any
			// change to them (a turn, an undo, a new paste) rebuilds it and
			// nothing else does.
			const std::uint64_t pendingVersion = edit.placement.voxels.Version();
			if (!edit.placing) {
				placementModel = Handle<client::IModel>();
				placementVoxels = Handle<VoxelModel>();
			} else if (!placementModel || placementModelVersion != pendingVersion) {
				const IntVector3 extent = edit.placement.Extent();
				Handle<VoxelModel> preview = Handle<VoxelModel>::New(extent.x, extent.y, extent.z);
				for (const ClipVoxel& v : *edit.placement.voxels)
					preview->SetSolid(v.rel.x, v.rel.y, v.rel.z, v.color);
				placementModel = renderer->CreateModel(*preview);
				placementVoxels = preview;
				placementModelVersion = pendingVersion;
			}
		}

		// --- Outlines --------------------------------------------------------

		void VoxelEditor::DrawLine3D(const Vector3& a, const Vector3& b, const Vector4& color) {
			overlayLines.Emit(a, b, color);
		}

		void VoxelEditor::DrawCellOutline(int x, int y, int z, const Vector4& color) {
			Vector3 a = MakeVector3(float(x) - 0.5F, float(y) - 0.5F, float(z) - 0.5F);
			Vector3 b = MakeVector3(float(x) + 0.5F, float(y) + 0.5F, float(z) + 0.5F);
			ForEachBoxEdge(a, b,
			               [&](const Vector3& p, const Vector3& q) { overlayLines.Emit(p, q, color); });
		}

		void VoxelEditor::DrawBoxOutline(const IntVector3& lo, const IntVector3& hi,
		                                 const Vector4& color) {
			Vector3 a = MakeVector3(float(lo.x) - 0.5F, float(lo.y) - 0.5F, float(lo.z) - 0.5F);
			Vector3 b = MakeVector3(float(hi.x) + 0.5F, float(hi.y) + 0.5F, float(hi.z) + 0.5F);
			ForEachBoxEdge(a, b,
			               [&](const Vector3& p, const Vector3& q) { overlayLines.Emit(p, q, color); });
		}

		void VoxelEditor::DrawCellOutlineMirrored(int x, int y, int z, const Vector4& color) {
			std::vector<IntVector3> cells;
			cells.push_back(MakeIntVector3(x, y, z));
			ExpandMirrors(cells);
			for (const IntVector3& c : cells)
				DrawCellOutline(c.x, c.y, c.z, color);
		}

		void VoxelEditor::DrawBoxOutlineMirrored(const IntVector3& lo, const IntVector3& hi,
		                                         const Vector4& color) {
			for (int mx = 0; mx <= (MirrorEnabled(0) ? 1 : 0); mx++)
				for (int my = 0; my <= (MirrorEnabled(1) ? 1 : 0); my++)
					for (int mz = 0; mz <= (MirrorEnabled(2) ? 1 : 0); mz++) {
						IntVector3 a = lo, b = hi;
						if (mx) {
							int p = MirrorIdx(lo.x, edit.mirror.plane.x),
							    q = MirrorIdx(hi.x, edit.mirror.plane.x);
							a.x = std::min(p, q);
							b.x = std::max(p, q);
						}
						if (my) {
							int p = MirrorIdx(lo.y, edit.mirror.plane.y),
							    q = MirrorIdx(hi.y, edit.mirror.plane.y);
							a.y = std::min(p, q);
							b.y = std::max(p, q);
						}
						if (mz) {
							int p = MirrorIdx(lo.z, edit.mirror.plane.z),
							    q = MirrorIdx(hi.z, edit.mirror.plane.z);
							a.z = std::min(p, q);
							b.z = std::max(p, q);
						}
						DrawBoxOutline(a, b, color);
					}
		}

		void VoxelEditor::BuildSelectionOutline() const {
			if (selectionOutline.version == edit.selection.Version())
				return;
			selectionOutline.version = edit.selection.Version();
			selectionOutline.edges.clear();

			// Only the edges on the outside of the selection. An edge four
			// selected voxels share is inside it: nothing can be seen of it,
			// and drawing one cage per voxel buries the shape of a selection
			// under its own hatching -- at twelve lines a voxel, which is what
			// made a large selection cost what it did every frame, even at
			// rest.
			const IntVector3 unit[3] = {MakeIntVector3(1, 0, 0), MakeIntVector3(0, 1, 0),
			                            MakeIntVector3(0, 0, 1)};
			const Vector3 half[3] = {MakeVector3(0.5F, 0.0F, 0.0F), MakeVector3(0.0F, 0.5F, 0.0F),
			                         MakeVector3(0.0F, 0.0F, 0.5F)};
			auto before = [](const IntVector3& p, const IntVector3& q) {
				return std::tie(p.x, p.y, p.z) < std::tie(q.x, q.y, q.z);
			};
			edit.selection.ForEach([&](const IntVector3& v) {
				for (int d = 0; d < 3; d++) {
					const int u = (d + 1) % 3, w = (d + 2) % 3;
					for (int su = -1; su <= 1; su += 2) {
						for (int sv = -1; sv <= 1; sv += 2) {
							// The four voxels this edge runs between: this one,
							// the two beside it, and the one diagonally across.
							const IntVector3 corner[4] = {v, v + unit[u] * su, v + unit[w] * sv,
							                              v + unit[u] * su + unit[w] * sv};
							bool taken[4] = {true, false, false, false};
							bool ours = true; // whether this voxel is the one to draw it
							int count = 1;
							for (int c = 1; c < 4; c++) {
								taken[c] = edit.selection.Contains(corner[c]);
								if (!taken[c])
									continue;
								count++;
								// Drawn once, by the first of the voxels sharing
								// it: the same edge is on all four of them.
								if (before(corner[c], v))
									ours = false;
							}
							if (!ours)
								continue;
							// Where the selection's surface bends, and nowhere
							// else. All four voxels selected and the edge is
							// buried inside it; two of them side by side and the
							// surface runs flat through it, which is every edge
							// ruled across a face. What is left is the shape of
							// the selection: its silhouette and its steps.
							if (count == 4 || (count == 2 && (taken[1] || taken[2])))
								continue;
							const Vector3 at = MakeVector3(float(v.x), float(v.y), float(v.z)) +
							                   half[u] * float(su) + half[w] * float(sv);
							selectionOutline.edges.push_back({at - half[d], at + half[d]});
						}
					}
				}
			});
		}

		void VoxelEditor::DrawSelection() {
			const Vector4 col = MakeVector4(1.0F, 0.55F, 0.1F, 0.95F);
			// Worked out once per selection and kept: at rest a frame only
			// hands the lines over again.
			BuildSelectionOutline();
			for (const SelectionEdge& e : selectionOutline.edges)
				overlayLines.Emit(e.a, e.b, col);
		}

		// --- Undo / redo ------------------------------------------------------

		void VoxelEditor::WriteVoxel(int x, int y, int z, bool solid, std::uint32_t color) {
			if (!Editable(x, y, z))
				return;
			if (!solid)
				edit.selection.Remove(MakeIntVector3(x, y, z)); // it holds solid voxels only
			const bool oldSolid = document.IsSolid(x, y, z);
			const std::uint32_t oldColor = document.Color(x, y, z);
			const std::uint32_t newColor = solid ? (color & 0xFFFFFF) : oldColor; // air keeps its colour
			document.Write(x, y, z, solid, newColor);
			undo.RecordVoxel(x, y, z, oldSolid, oldColor, solid, newColor);
		}

		void VoxelEditor::UndoApplyVoxel(int x, int y, int z, bool solid, std::uint32_t color) {
			document.Write(x, y, z, solid, color);
		}

		void VoxelEditor::UndoApplyReframe(int w, int h, int d, int ox, int oy, int oz) {
			ReframeRaw(MakeIntVector3(w, h, d), MakeIntVector3(ox, oy, oz));
		}

		void VoxelEditor::UndoApplyOrigin(const Vector3& origin) { document.SetOrigin(origin); }

		// Undo and redo replay the one history, pending voxels included: a
		// move or a turn is a step like any edit, so nothing is placed first.
		void VoxelEditor::Undo() {
			// Object mode steps through the scene's own history of placements.
			if (currentMode == EditorMode::Object) {
				if (IObjectModeHost* objects = ObjectMode()) {
					if (objects->CanUndoObjects())
						objects->UndoObjects();
					else
						SetStatus("Nothing to undo");
				}
				return;
			}
			EndPreviews(); // the history holds only what was committed
			std::string label = undo.UndoLabel();
			if (!undo.Undo()) {
				SetStatus("Nothing to undo");
				return;
			}
			SetStatus("Undid " + (label.empty() ? std::string("edit") : label));
			HistoryReplayed();
		}

		void VoxelEditor::Redo() {
			if (currentMode == EditorMode::Object) {
				if (IObjectModeHost* objects = ObjectMode()) {
					if (objects->CanRedoObjects())
						objects->RedoObjects();
					else
						SetStatus("Nothing to redo");
				}
				return;
			}
			EndPreviews(); // the history holds only what was committed
			std::string label = undo.RedoLabel();
			if (!undo.Redo()) {
				SetStatus("Nothing to redo");
				return;
			}
			SetStatus("Redid " + (label.empty() ? std::string("edit") : label));
			HistoryReplayed();
		}

		void VoxelEditor::HistoryReplayed() {
			// The middle a moved selection turns about is no part of the history;
			// the selection the replay restored starts again from its box.
			movedMiddle.valid = false;
			if (edit.placing)
				ActivateTransformTool();
			NotifyDocumentChanged();
		}

		// --- Pivot ------------------------------------------------------------

		Vector3 VoxelEditor::GetPivot() const { return document.Origin() * -1.0F; }

		void VoxelEditor::SetPivot(const Vector3& pivot) {
			DocumentCommand command(*this);
			Vector3 before = document.Origin();
			Vector3 after = pivot * -1.0F;
			if (after.x == before.x && after.y == before.y && after.z == before.z)
				return;
			VoxelUndoStack::Step step(undo, "Set Pivot");
			document.SetOrigin(after);
			undo.RecordOrigin(before, after);
		}

		// Live, non-journaled pivot move for a drag in progress; the tool commits the
		// net change with one SetPivot on release.
		void VoxelEditor::PreviewPivot(const Vector3& pivot) {
			if (!previewedOrigin)
				previewedOrigin = document.Origin();
			document.SetOrigin(pivot * -1.0F);
		}

		// --- Reference images -------------------------------------------------

		namespace {
			const char* const kAxisName[3] = {"X", "Y", "Z"};
		} // namespace

		ReferenceImage& VoxelEditor::Reference(int axis) {
			return references[Clamp(axis, 0, 2)];
		}

		void VoxelEditor::LoadReferenceImage(int axis) {
			axis = Clamp(axis, 0, 2);
			// The browser takes the input over while it is up.
			ReleaseHeldInput();
			host.PickImageFile(std::string("Reference Image Across ") + kAxisName[axis],
			                   [this, axis](const std::string& path) { LoadReferenceFile(axis, path); });
		}

		bool VoxelEditor::LoadReferenceFile(int axis, const std::string& path) {
			Handle<client::IImage> image;
			int width = 0, height = 0;
			try {
				std::FILE* f = LocalFileSystem::OpenFile(path, "rb");
				if (!f)
					SPRaise("cannot open the file");
				StdStream stream(f, true); // takes ownership of the FILE*
				Handle<Bitmap> bitmap = Bitmap::Load(stream);
				width = bitmap->GetWidth();
				height = bitmap->GetHeight();
				// Its alpha is kept: what is transparent in it shows the document
				// and the rest of the image planes through it.
				image = renderer->CreateImage(*bitmap);
			} catch (const std::exception& ex) {
				SPLog("Could not load reference image %s: %s", path.c_str(), ex.what());
			}
			if (!image || width <= 0 || height <= 0) {
				SetStatus("Could not load " + LocalFileSystem::GetFileName(path));
				return false;
			}

			ReferenceImage& ref = references[axis];
			const bool first = !ref.Loaded();
			ref.path = path;
			ref.image = image;
			ref.pixelWidth = width;
			ref.pixelHeight = height;
			ref.visible = true;
			// A first image is fitted to the document: as wide as its volume
			// across the image, its middle on the volume's, and just behind the
			// volume, where it never cuts through the voxels being modelled. A
			// replacement keeps how the one before it was set up.
			if (first) {
				int u, v;
				ReferenceAxes(axis, u, v);
				// The box it is fitted to, from voxel edge to edge: the document's
				// volume, or with a scene's objects shown, all of them.
				const IntVector3 size = document.Size();
				Vector3 lo = MakeVector3(-0.5F, -0.5F, -0.5F);
				Vector3 hi = MakeVector3(float(size.x), float(size.y), float(size.z)) - 0.5F;
				if (currentMode == EditorMode::Object && ObjectMode())
					ObjectMode()->SceneBox(lo, hi);
				ref.size = std::max(1.0F, std::max(hi[u] - lo[u], hi[v] - lo[v]));
				Vector3& centre = ref.centre;
				centre[u] = (lo[u] + hi[u]) * 0.5F;
				centre[v] = (lo[v] + hi[v]) * 0.5F;
				// Behind the voxels as each view is usually looked from: the side
				// from +X, the front from -Y, and the top from above (Z points
				// down, so above is the low end of Z).
				centre[axis] = axis == 0 ? lo.x - 0.5F : axis == 1 ? hi.y + 0.5F : hi.z + 0.5F;
				ref.rotation = MakeVector4(0.0F, 0.0F, 0.0F, 1.0F);
			}
			SetStatus(std::string("Reference image across ") + kAxisName[axis] + ": " +
			          LocalFileSystem::GetFileName(path));
			return true;
		}

		void VoxelEditor::ClearReferenceImage(int axis) {
			ReferenceImage& ref = references[Clamp(axis, 0, 2)];
			if (!ref.Loaded())
				return;
			const ReferenceImage cleared;
			// The placement stays, so loading another image puts it where this was.
			ReferenceImage kept = cleared;
			kept.size = ref.size;
			kept.centre = ref.centre;
			kept.rotation = ref.rotation;
			kept.opacity = ref.opacity;
			kept.flipped = ref.flipped;
			ref = kept;
			SetStatus(std::string("Reference image across ") + kAxisName[axis] + " removed");
		}

		void VoxelEditor::DrawReferenceImages() {
			for (int axis = 0; axis < 3; axis++) {
				const ReferenceImage& ref = references[axis];
				if (!ref.Loaded() || !ref.visible || ref.opacity <= 0.0F)
					continue;
				Vector3 frame[3];
				ReferenceFrame(axis, ref.rotation, frame);
				const Vector3 alongU = frame[0] * (ref.flipped ? -ref.size : ref.size);
				const Vector3 alongV = frame[1] * ref.Height();
				const Vector3 origin =
				  ref.centre - alongU * 0.5F - alongV * 0.5F + host.DocumentOrigin();
				const float o = Clamp(ref.opacity, 0.0F, 1.0F);
				renderer->SetColorAlphaPremultiplied(MakeVector4(o, o, o, o));
				renderer->AddImagePlane(*ref.image, origin, alongU, alongV);
			}
		}

		// --- Mirror -----------------------------------------------------------

		bool VoxelEditor::MirrorEnabled(int axis) const {
			if (axis < 0 || axis > 2)
				return false;
			// Mirroring is a modelling aid for Edit mode; it must not silently
			// reshape anything while another mode is driving the document.
			if (currentMode != EditorMode::Edit)
				return false;
			return edit.mirror.enabled[axis];
		}

		// The mirror setup is journaled like the selection (each undo step
		// restores it), so changing it is a command like any edit.
		void VoxelEditor::SetMirrorEnabled(int axis, bool on) {
			if (axis < 0 || axis > 2)
				return;
			DocumentCommand command(*this);
			VoxelUndoStack::Step step(undo, "Mirror Axis");
			edit.mirror.enabled[axis] = on;
		}

		void VoxelEditor::SetMirrorPlane(const Vector3& plane) {
			DocumentCommand command(*this);
			VoxelUndoStack::Step step(undo, "Move Mirror");
			PlaceMirrorPlane(plane);
		}

		void VoxelEditor::PreviewMirrorPlane(const Vector3& plane) {
			if (!previewedMirrorPlane)
				previewedMirrorPlane = edit.mirror.plane;
			PlaceMirrorPlane(plane);
		}

		void VoxelEditor::ResetMirrorPlane() {
			DocumentCommand command(*this);
			VoxelUndoStack::Step step(undo, "Reset Mirror");
			PlaceMirrorPlane(GetPivot());
		}

		void VoxelEditor::PlaceMirrorPlane(const Vector3& plane) {
			// MirrorIdx only sees whole half steps, so anything finer would move the
			// handle without moving the reflection.
			auto snap = [](float v) { return float(HalfSteps(v)) * 0.5F; };
			edit.mirror.plane = MakeVector3(snap(plane.x), snap(plane.y), snap(plane.z));
		}

		// Semi-transparent quad at each enabled mirror plane (drawn as a fan of
		// debug lines, since the editor only has line primitives in 3D).
		void VoxelEditor::DrawMirrorPlanes() {
			const IntVector3 size = document.Size();
			const int w = size.x, h = size.y, d = size.z;

			// Grid lines are spaced one tile (2 voxels) apart. Extend one tile past
			// the volume on every side, rounding the far edge up to the tile grid so
			// the plane is a whole number of tiles and always shows a complete border
			// (the volume spans [-0.5, dim-0.5]; the near edge -2.5 is already aligned).
			auto stop = [](int n) { return (n % 2 == 0) ? n + 2 : n + 3; };
			const Vector3 origin = host.DocumentOrigin();
			auto line = [&](const Vector3& p, const Vector3& q, const Vector4& color) {
				renderer->AddDebugLine(p + origin, q + origin, color);
			};
			float lo = -2.5F;
			float hiX = float(stop(w)) - 0.5F;
			float hiY = float(stop(h)) - 0.5F;
			float hiZ = float(stop(d)) - 0.5F;

			if (MirrorEnabled(0)) {
				float px = edit.mirror.plane.x;
				Vector4 col = MakeVector4(1.0F, 0.35F, 0.35F, 0.25F);
				for (int i = -2; i <= stop(h); i += 2)
					line(MakeVector3(px, float(i) - 0.5F, lo),
					                       MakeVector3(px, float(i) - 0.5F, hiZ), col);
				for (int i = -2; i <= stop(d); i += 2)
					line(MakeVector3(px, lo, float(i) - 0.5F),
					                       MakeVector3(px, hiY, float(i) - 0.5F), col);
			}
			if (MirrorEnabled(1)) {
				float py = edit.mirror.plane.y;
				Vector4 col = MakeVector4(0.4F, 1.0F, 0.4F, 0.25F);
				for (int i = -2; i <= stop(w); i += 2)
					line(MakeVector3(float(i) - 0.5F, py, lo),
					                       MakeVector3(float(i) - 0.5F, py, hiZ), col);
				for (int i = -2; i <= stop(d); i += 2)
					line(MakeVector3(lo, py, float(i) - 0.5F),
					                       MakeVector3(hiX, py, float(i) - 0.5F), col);
			}
			if (MirrorEnabled(2)) {
				float pz = edit.mirror.plane.z;
				Vector4 col = MakeVector4(0.45F, 0.6F, 1.0F, 0.25F);
				for (int i = -2; i <= stop(w); i += 2)
					line(MakeVector3(float(i) - 0.5F, lo, pz),
					                       MakeVector3(float(i) - 0.5F, hiY, pz), col);
				for (int i = -2; i <= stop(h); i += 2)
					line(MakeVector3(lo, float(i) - 0.5F, pz),
					                       MakeVector3(hiX, float(i) - 0.5F, pz), col);
			}
		}

		// --- Transform gizmo -------------------------------------------------

		GizmoView VoxelEditor::GetGizmoView() const { return DocumentView(); }

		void VoxelEditor::DrawGizmo(const TransformGizmo& gizmo) {
			GizmoCanvas canvas(*renderer);
			gizmo.Draw(canvas, GetGizmoView());
		}

		void VoxelEditor::DrawScreenRect(const AABB2& rect, const Vector4& color) {
			// A faint fill to see what it takes in, and its edge.
			Vector4 fill = color;
			fill.w *= 0.12F;
			OverlayColorNP(*renderer, fill);
			OverlayFillRect(*renderer, rect.GetMinX(), rect.GetMinY(), rect.GetWidth(),
			                rect.GetHeight());
			OverlayStrokeRect(*renderer, rect.GetMinX(), rect.GetMinY(), rect.GetWidth(),
			                  rect.GetHeight(), 1.0F, color);
		}

		void VoxelEditor::DrawScreenCircle(const Vector2& centre, float radius,
		                                   const Vector4& color) {
			Vector4 fill = color;
			fill.w *= 0.08F;
			OverlayFillCircle(*renderer, centre, radius, fill);
			OverlayStrokeCircle(*renderer, centre, radius, 1.5F, color);
		}

		// --- Tools and modes --------------------------------------------------

		std::string VoxelEditor::ToolLabel(int index) const {
			if (index < 0 || index >= int(tools.size()))
				return std::string();
			return tools[static_cast<size_t>(index)].tool->Label();
		}

		bool VoxelEditor::ToolAvailable(int index) const {
			return index >= 0 && index < int(tools.size()) &&
			       tools[static_cast<size_t>(index)].tool->Available(*this);
		}

		void VoxelEditor::SelectTool(int index) {
			SetMode(EditorMode::Edit);
			SetActiveTool(index);
		}

		void VoxelEditor::SetActiveTool(int index) {
			if (index < 0 || index >= int(tools.size()))
				return;
			// A tool belongs to one mode: in the other, its button is not shown
			// and its hot key does nothing.
			if (currentMode == EditorMode::Object) {
				if (InObjectMode(index))
					SetObjectModeTool(index);
				return;
			}
			if (InObjectMode(index))
				return;
			if (!ToolAvailable(index)) {
				SetStatus("Select some voxels first");
				return;
			}
			Activate(index, currentMode);
		}

		bool VoxelEditor::InObjectMode(int index) const {
			return host.ObjectMode() && index >= 0 && index < int(tools.size()) &&
			       tools[std::size_t(index)].id == "reference";
		}

		void VoxelEditor::SetObjectModeTool(int index) {
			if (index == objectModeTool || (index >= 0 && !InObjectMode(index)))
				return;
			ui.GetOptionBar()->EndEditing(true);
			VoxelTool* previous = ActiveTool();
			objectModeTool = index;
			VoxelTool* current = ActiveTool();
			if (previous == current)
				return;
			if (previous)
				previous->OnDeactivate(*this);
			if (current)
				current->OnActivate(*this);
		}

		void VoxelEditor::SetMode(EditorMode mode) { Activate(activeTool, mode); }

		void VoxelEditor::SetObjectMode(bool on) {
			SetMode(on && ModeSupported(EditorMode::Object) ? EditorMode::Object : EditorMode::Edit);
		}

		bool VoxelEditor::ModeSupported(EditorMode mode) const {
			switch (mode) {
				case EditorMode::Edit: return true;
				case EditorMode::Object: return host.ObjectMode() != nullptr;
				case EditorMode::Animation: return false;
			}
			return false;
		}

		IObjectModeHost* VoxelEditor::ObjectMode() { return host.ObjectMode(); }

		void VoxelEditor::Activate(int toolIndex, EditorMode mode) {
			if (toolIndex == activeTool && mode == currentMode)
				return;
			// Edit mode edits the picked object of a scene, so a scene with none
			// picked stays in Object mode.
			IObjectModeHost* objects = ObjectMode();
			if (mode == EditorMode::Edit && currentMode != EditorMode::Edit && objects &&
			    !objects->HasPickedObject()) {
				SetStatus("Pick an object to edit first");
				return;
			}
			// A number being typed into the sub-toolbar, or the panel, belongs to
			// the tool whose box it is: it is settled here, while that tool is
			// still the one listening. Left to the bar, the edit would be dropped
			// on the next rebuild with whatever it was previewing still showing.
			SettlePanel();
			EndBarEdits(true);
			// Leaving a tool ends what it had in progress: this is where a pending
			// placement reaches the document.
			if (VoxelTool* previous = ActiveTool())
				previous->OnDeactivate(*this);
			const bool wasEditing = currentMode == EditorMode::Edit;
			if (wasEditing && mode != EditorMode::Edit) {
				EndUserAction(); // nothing previewed is carried out of the document
				// No colour is chosen outside Edit mode: its picker goes with it.
				ui.GetColorPicker()->SetEyedropperMode(false);
				ui.GetColorPicker()->Close();
			}
			if (mode == EditorMode::Edit && InObjectMode(toolIndex))
				toolIndex = defaultTool;
			activeTool = toolIndex;
			currentMode = mode;
			// The scene takes the voxels back as Edit mode is left, and hands the
			// picked object's over as it is entered.
			const bool editing = mode == EditorMode::Edit;
			if (objects && wasEditing != editing)
				objects->EditModeChanged(editing);
			if (VoxelTool* current = ActiveTool())
				current->OnActivate(*this);
			// The picker shows the colour it now sets, which may be another's.
			if (ui.GetColorPicker()->IsOpen())
				ui.GetColorPicker()->SetColor(PickerColor());
		}

		const char* const VoxelEditor::kModeToggleKey = "Tab";

		void VoxelEditor::OpenModeMenu(const AABB2& anchor) {
			CancelToolInteraction();
			std::vector<ContextMenuItem> items;
			for (const ModeInfo& info : Modes()) {
				if (!ModeSupported(info.mode))
					continue;
				const EditorMode mode = info.mode;
				ContextMenuItem item(std::string(info.label) + " Mode", [this, mode] { SetMode(mode); });
				item.Checked(mode == currentMode);
				// Tab goes between these two: it says so on both.
				if (mode == EditorMode::Object || mode == EditorMode::Edit)
					item.Shortcut(kModeToggleKey);
				items.push_back(std::move(item));
			}
			ui.GetContextMenu()->OpenBelow(anchor, std::move(items), this);
		}

		const std::vector<VoxelEditor::ModeInfo>& VoxelEditor::Modes() {
			static const std::vector<ModeInfo> modes = {
			  {EditorMode::Object, "object", "Object"},
			  {EditorMode::Edit, "edit", "Edit"},
			  {EditorMode::Animation, "animation", "Animation"}};
			return modes;
		}

		int VoxelEditor::ToolIndex(const std::string& id) const {
			for (size_t i = 0; i < tools.size(); i++) {
				if (tools[i].id == id)
					return int(i);
			}
			return -1;
		}

		int VoxelEditor::StripToolIndex() const {
			if (currentMode != EditorMode::Edit || activeTool < 0 ||
			    activeTool >= int(tools.size()))
				return -1;
			const VoxelToolSlot& current = tools[static_cast<size_t>(activeTool)];
			if (!current.tool->StripOf().empty())
				return activeTool; // lit on its own strip, whatever is left to it
			for (size_t i = 0; i < tools.size(); i++) {
				if (tools[i].tool->StripOf() == current.id && ToolAvailable(int(i)))
					return int(i);
			}
			return -1;
		}

		VoxelTool* VoxelEditor::StripTool() {
			const int index = StripToolIndex();
			return index < 0 ? nullptr : tools[static_cast<size_t>(index)].tool.get();
		}

		ToolOption* VoxelEditor::FindOption(VoxelTool* tool, const std::string& id,
		                                    ToolOption::Type type) {
			if (!tool)
				return nullptr;
			ToolOptions* options = tool->Options();
			ToolOption* option = options ? options->Find(id) : nullptr;
			if (!option) {
				ToolOptions* commands = tool->StripCommands();
				option = commands ? commands->Find(id) : nullptr;
			}
			return (option && option->type == type) ? option : nullptr;
		}

		int VoxelEditor::BarToolIndex() const {
			if (currentMode != EditorMode::Edit || activeTool < 0 ||
			    activeTool >= int(tools.size()))
				return -1;
			const int host = ToolIndex(tools[static_cast<size_t>(activeTool)].tool->StripOf());
			return host >= 0 ? host : activeTool;
		}

		VoxelTool* VoxelEditor::BarTool() {
			const int index = BarToolIndex();
			return index < 0 ? nullptr : tools[static_cast<size_t>(index)].tool.get();
		}

		// A sub-tool's key is its own where it declares one, and otherwise its
		// place in the bar. A sub-tool that was a tool of its own keeps the key
		// it was known by (Transform's T), and the digits stay where they are
		// for the rest, so nothing shifts under the user when one is added.
		std::string VoxelEditor::SubToolHotKey(VoxelTool& tool, int index) {
			if (auto* sub = tool.SubTool(index)) {
				const std::string own = sub->HotKey();
				if (!own.empty())
					return own;
			}
			// A strip is not the bar the digits count along.
			if (!tool.StripOf().empty())
				return std::string();
			return (index >= 0 && index < 9) ? std::to_string(index + 1) : std::string();
		}

		bool VoxelEditor::KeyIsTaken(const std::string& key) const {
			return host.KeyIsReserved(key) || EditorKeyMatches(cg_keyDelete, key);
		}

		bool VoxelEditor::SwitchByHotKey(const std::string& key) {
			// Never mid-press: the tool taking over would see a drag it never saw start.
			if (key.empty() || lmbHeld || rmbHeld || KeyIsTaken(key))
				return false;
			for (size_t i = 0; i < tools.size(); i++) {
				// Only the tools of the mode the editor is in.
				if ((currentMode == EditorMode::Object) != InObjectMode(int(i)))
					continue;
				if (!tools[i].hotKey.empty() && EqualsIgnoringCase(tools[i].hotKey, key)) {
					SetActiveTool(int(i));
					return true;
				}
			}
			// A key a sub-tool declares as its own (Transform's T) is good from
			// any tool; the digits, being only a place in the active tool's
			// bar, are not.
			// The digits count along the bar that is showing, which a strip
			// leaves to the tool it is the strip of.
			VoxelTool* active = BarTool();
			for (size_t i = 0; i < tools.size(); i++) {
				VoxelTool& tool = *tools[i].tool;
				for (int sub = 0; sub < tool.SubToolCount(); sub++) {
					const std::string subKey = SubToolHotKey(tool, sub);
					auto* candidate = tool.SubTool(sub);
					const bool own = candidate && !candidate->HotKey().empty();
					if ((!own && &tool != active) || !EqualsIgnoringCase(subKey, key))
						continue;
					SetActiveTool(int(i)); // says why not, when it has nothing to work on
					if (activeTool == int(i))
						tool.SetSubTool(*this, sub);
					return true;
				}
			}
			return false;
		}

		VoxelTool* VoxelEditor::ActiveTool() {
			if (currentMode == EditorMode::Object && ObjectMode()) {
				if (InObjectMode(objectModeTool))
					return tools[std::size_t(objectModeTool)].tool.get();
				return objectTool.get();
			}
			if (currentMode == EditorMode::Edit && activeTool >= 0 && activeTool < int(tools.size()) &&
			    !InObjectMode(activeTool))
				return tools[activeTool].tool.get();
			return nullptr;
		}

		// --- Input ------------------------------------------------------------

		PointerInput VoxelEditor::MakePointer(PointerButton b, PointerPhase ph,
		                                      const Vector2& delta) const {
			PointerInput e;
			e.button = b;
			e.phase = ph;
			e.pos = host.AimPosition();
			e.delta = delta;
			e.alt = altHeld;
			e.ctrl = ctrlHeld;
			e.shift = shiftHeld;
			return e;
		}

		void VoxelEditor::DispatchPointer(const PointerInput& e) {
			// A press starts one user action and the release of the last held
			// button ends it, so a stroke or a drag undoes as one step. (The held
			// flags already include this press and exclude this release.)
			if (e.IsDown() && !(lmbHeld && rmbHeld))
				undo.BeginAction();
			UserActionEnd end(*this, e.IsUp() && !lmbHeld && !rmbHeld);
			VoxelTool* t = ActiveTool();
			if (!t)
				return;
			// The right button is the inverse of the left, and a tool with no
			// inverse (Paint) takes nothing from it, whatever the sub-tool.
			if (e.IsRight() && !t->RightButtonInverts())
				return;
			t->OnPointer(*this, e);
			SwitchAsAsked();
		}

		void VoxelEditor::SwitchAsAsked() {
			// After the tool is done with the event, never from inside it; as
			// part of the same user action, so it undoes with it.
			if (pendingTool.empty())
				return;
			const std::string id = pendingTool;
			pendingTool.clear();
			if (clearingSelection) {
				clearingSelection = false;
				if (!edit.selection.Empty()) {
					ClearSelection();
					SetStatus("Selection cleared");
				}
			}
			SetActiveTool(ToolIndex(id));
		}

		void VoxelEditor::CancelToolInteraction() {
			SettlePanel();
			// A colour being chosen is abandoned like a drag: its live recolour
			// is put back, and the colour stays the brush colour.
			if (pickerHeld)
				EndPickerPress(false);
			if (VoxelTool* t = ActiveTool())
				t->CancelInteraction(*this);
			EndUserAction(); // whatever comes next is a separate step
		}

		void VoxelEditor::EndUserAction() noexcept {
			EndPreviews();
			undo.EndAction();
		}

		void VoxelEditor::EndPreviews() noexcept {
			if (previewedOrigin) {
				document.SetOrigin(*previewedOrigin);
				previewedOrigin.reset();
			}
			if (previewedMirrorPlane) {
				edit.mirror.plane = *previewedMirrorPlane;
				previewedMirrorPlane.reset();
			}
			if (previewedColors) {
				for (const auto& voxel : *previewedColors) {
					const IntVector3& v = voxel.first;
					document.Write(v.x, v.y, v.z, true, voxel.second);
				}
				previewedColors.reset();
			}
			if (previewedPlacement) {
				edit.placement = std::move(*previewedPlacement);
				previewedPlacement.reset();
			}
		}

		void VoxelEditor::NotifyDocumentChanged() {
			if (VoxelTool* t = ActiveTool())
				t->OnDocumentChanged(*this);
		}

		void VoxelEditor::TrackModifiers(const std::string& key, bool down) {
			if (key == "Control")
				ctrlHeld = down;
			if (key == "Alt")
				altHeld = down;
			if (key == "Shift")
				shiftHeld = down;
			if (key == "Meta")
				metaHeld = down;
		}

		bool VoxelEditor::IsEditShortcut(const std::string& key) {
			for (const char* k : {"a", "i", "c", "x", "v", "z", "y"}) {
				if (EqualsIgnoringCase(key, k))
					return true;
			}
			return false;
		}

		void VoxelEditor::RunEditShortcut(const std::string& key) {
			// A shortcut changes the document or the placement under a drag in
			// progress (a paste replaces it), so that drag is abandoned.
			CancelToolInteraction();
			// Selecting, copying, cutting and pasting are voxels', which only
			// Edit mode has.
			const bool editing = currentMode == EditorMode::Edit;
			if (editing && EqualsIgnoringCase(key, "a")) {
				// Select All, and with Shift the other way about, as a list in
				// any application does it.
				if (shiftHeld)
					ClearSelection();
				else
					SelectAll();
			} else if (editing && EqualsIgnoringCase(key, "i"))
				InvertSelection();
			else if (editing && EqualsIgnoringCase(key, "c"))
				CopySelection();
			else if (editing && EqualsIgnoringCase(key, "x"))
				CutSelection();
			else if (editing && EqualsIgnoringCase(key, "v"))
				Paste();
			else if (EqualsIgnoringCase(key, "z")) {
				if (shiftHeld)
					Redo();
				else
					Undo();
			} else if (EqualsIgnoringCase(key, "y"))
				Redo();
		}

		void VoxelEditor::PointerMoved(const Vector2& delta) {
			// While a dialog is up the cursor drives its widgets, not the editor.
			if (ui.GetOverlay()->IsActive())
				return;
			ITransformTarget* target = PanelTarget();
			// A number being scrubbed takes the motion; over the panel, nothing
			// under it answers to the pointer.
			if (transformPanel->IsHolding()) {
				if (target)
					transformPanel->Drag(*target, delta.x, shiftHeld, ctrlHeld);
				return;
			}
			const Vector2& at = cursor.GetPosition();
			ui.GetMenuBar()->MouseMove(at);
			if (ui.GetMenuBar()->IsOpen())
				return; // the menu has the pointer while it is down
			if (target && OnPanel(at))
				return;
			if (ui.GetColorPicker()->IsOpen())
				ui.GetColorPicker()->MouseMove(at);

			// Motion over the viewport reaches the active tool as a Move (no
			// button) or Drag (button held) event.
			if (at.y < BarsHeight())
				return;
			PointerButton b = lmbHeld ? PointerButton::Left
			                          : (rmbHeld ? PointerButton::Right : PointerButton::None);
			PointerPhase ph = (lmbHeld || rmbHeld) ? PointerPhase::Drag : PointerPhase::Move;
			DispatchPointer(MakePointer(b, ph, delta));
		}

		void VoxelEditor::ButtonEvent(PointerButton button, bool down) {
			const Vector2& at = cursor.GetPosition();

			// Every button is the open menu's while it is down. The left one
			// has somewhere to land -- a command, a title -- and is answered
			// below, where the pointer is; the others only put it away, and do
			// nothing of what they would have done to the model behind it.
			if (down && button != PointerButton::Left && ui.GetMenuBar()->IsOpen()) {
				ui.GetMenuBar()->Close();
				return;
			}

			// A release reaches the tool only when its press did: presses the
			// picker, the bars, the host's widgets or colour sampling took are
			// none of the tool's business, and start no user action to end.
			ITransformTarget* target = PanelTarget();
			const bool onPanel = target && OnPanel(at);
			// A number being typed is settled by a press anywhere but on its
			// panel, which answers for its own fields.
			if (down && !onPanel)
				SettlePanel();

			if (button == PointerButton::Left) {
				if (!down && transformPanel->IsHolding()) {
					if (target)
						transformPanel->Release(*target, clock);
					return;
				}
				if (down && onPanel) {
					EndBarEdits(true);
					transformPanel->Press(*target, at);
					return;
				}
				if (!down) {
					ui.GetColorPicker()->MouseUp();
					if (pickerHeld)
						EndPickerPress(true);
					if (lmbHeld) {
						lmbHeld = false;
						DispatchPointer(MakePointer(PointerButton::Left, PointerPhase::Up));
					}
					return;
				}

				// A number being typed into the sub-toolbar is settled by a press
				// anywhere but on that bar, which answers for its own boxes.
				if (at.y < kRibbonH + kToolbarH || at.y >= BarsHeight())
					ui.GetOptionBar()->EndEditing(true);
				if (!ui.GetContextStrip()->Contains(at))
					ui.GetContextStrip()->EndEditing(true);

				// A menu of the bar owns every press on itself and on its titles,
				// and a press anywhere else puts it away before that press does
				// whatever it does.
				// A menu of the bar owns every press while it is down: on itself
				// it picks a command, and anywhere else it closes and that is
				// all that press does.
				if (ui.GetMenuBar()->IsOpen() && ui.GetMenuBar()->Click(at))
					return;


				// The open picker owns every press on its panel, gaps included, so
				// nothing behind it is ever edited through it.
				if (ui.GetColorPicker()->IsOverPicker(at)) {
					// The press is one user action, ended by its release, so a drag
					// across the colours undoes as one step, whatever it recoloured.
					if (!rmbHeld)
						undo.BeginAction();
					pickerHeld = true;
					pickerChangedColor = false;
					ui.GetColorPicker()->MouseDown(at);
					return;
				}

				// Over the bars, a click belongs to whichever button is under it
				// (the callbacks wired in WireUI act on it), or to nothing.
				if (at.y < BarsHeight()) {
					if (ui.GetMenuBar()->Click(at)) {
						// A menu taking the keyboard means nothing may stay held
						// from before it was opened.
						ReleaseHeldInput();
						return;
					}
					if (!ui.GetToolbar()->Click(at, screenWidth))
						ui.GetOptionBar()->Click(at);
					// Typing into a box means the keyboard is the box's now, so
					// nothing may stay held from before it was clicked.
					if (ui.GetOptionBar()->IsEditing())
						ReleaseHeldInput();
					return;
				}

				// The strip floats over the viewport: a press on it is its own,
				// button or not, and never reaches the model behind it.
				if (ui.GetContextStrip()->Contains(at)) {
					ui.GetContextStrip()->Click(at);
					if (ui.GetContextStrip()->IsEditing())
						ReleaseHeldInput();
					return;
				}

				if (host.ClickViewportWidget(at))
					return;

				// Sampling a colour works the same in every tool, which never sees
				// the click.
				if (SamplingArmed()) {
					SampleColor();
					return;
				}

				lmbHeld = true;
				DispatchPointer(MakePointer(PointerButton::Left, PointerPhase::Down));
				return;
			}

			if (button == PointerButton::Right) {
				if (down && onPanel)
					return; // nothing to do on it
				if (!down) {
					if (rmbHeld) {
						rmbHeld = false;
						DispatchPointer(MakePointer(PointerButton::Right, PointerPhase::Up));
					}
					return;
				}
				// Not over the colour picker or the bars.
				if (ui.GetColorPicker()->IsOverPicker(at) || at.y < BarsHeight() ||
				    ui.GetContextStrip()->Contains(at))
					return;
				rmbHeld = true;
				DispatchPointer(MakePointer(PointerButton::Right, PointerPhase::Down));
			}
		}

		bool VoxelEditor::OptionBarKey(const std::string& key) {
			if (transformPanel->IsTyping()) {
				if (ITransformTarget* target = PanelTarget()) {
					TextEntry::Modifiers m;
					m.shift = shiftHeld;
					m.ctrl = ctrlHeld;
					m.alt = altHeld;
					m.meta = metaHeld;
					transformPanel->Key(*target, key, m, clock);
				}
				return true;
			}
			return ui.GetOptionBar()->KeyEvent(key) || ui.GetContextStrip()->KeyEvent(key);
		}

		bool VoxelEditor::CommandKey(const std::string& key, bool down) {
			// Tab goes between the scene's objects and the picked one's voxels,
			// as in Blender.
			if (down && key == kModeToggleKey && ModeSupported(EditorMode::Object) && !lmbHeld &&
			    !rmbHeld) {
				CancelToolInteraction();
				SetObjectMode(currentMode != EditorMode::Object);
				return true;
			}
			if (down && currentMode == EditorMode::Edit && EditorKeyMatches(cg_keyDelete, key)) {
				// Like a Ctrl shortcut, it edits behind the tool's back.
				CancelToolInteraction();
				DeleteSelection();
				return true;
			}
			return false;
		}

		void VoxelEditor::ToolKey(const std::string& key, bool down) {
			// Tool and sub-tool keys, shown on their buttons. Plain keys only, so
			// a chord never switches tools.
			if (down && !ctrlHeld && !altHeld && SwitchByHotKey(key))
				return;

			// Remaining keys go to the active tool (e.g. Select's [L]).
			VoxelTool* t = ActiveTool();
			if (!t)
				return;
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
			SwitchAsAsked();
		}

		void VoxelEditor::ReleaseHeldInput() {
			host.ReleaseHeldKeys();
			// The release of a held button may be swallowed too, so the press it
			// began is over: the tool and the colour picker drop their drags and
			// nothing reads as dragged.
			lmbHeld = rmbHeld = false;
			ui.GetColorPicker()->MouseUp();
			CancelToolInteraction();
		}

		// --- Frame ------------------------------------------------------------

		void VoxelEditor::Update(float dt) {
			clock += dt;
			if (statusTimer > 0.0F)
				statusTimer -= dt;
			RefreshPlacementModel(); // what edits since the last frame left stale
			// A tool left with nothing to work on -- the selection deleted,
			// cleared or undone from under Modify -- hands over to Select.
			// Never mid-press: the tool taking over would see a drag it never
			// saw start.
			if (!lmbHeld && !rmbHeld && !ToolAvailable(activeTool)) {
				const int select = ToolIndex("select");
				if (ToolAvailable(select))
					SetActiveTool(select);
			}
		}

		// The view is the host's to turn and to make an image of.
		void VoxelEditor::LookIsometric(float elevation) { host.LookIsometric(elevation); }
		void VoxelEditor::TurnView(int quarterTurns) { host.TurnView(quarterTurns); }
		void VoxelEditor::SaveRender(std::uint32_t background, bool transparent) {
			host.SaveRender(background, transparent);
		}

		bool VoxelEditor::PresentsRender() {
			VoxelTool* tool = ActiveTool();
			return tool && tool->PresentsRender();
		}

		void VoxelEditor::DrawScene() {
			// An image of the model holds the model, which the host draws.
			if (PresentsRender())
				return;
			// Object mode shows the scene, which the host draws; what is drawn
			// below is the document's, Edit mode's.
			// The images to model from, whatever the tool: they are depth tested,
			// so the voxels in front of them hide them. A scene's are set up
			// among its objects and shown with them, in Object mode only.
			const bool scene = ObjectMode() != nullptr;
			if (currentMode != EditorMode::Edit) {
				if (scene && currentMode == EditorMode::Object)
					DrawReferenceImages();
				if (VoxelTool* tool = ActiveTool())
					tool->DrawScene(*this);
				return;
			}
			if (!scene)
				DrawReferenceImages();
			// Pending voxels (a paste, an import) render solid at their temporary
			// position, in a document that does not hold them yet.
			if (edit.placing && placementModel) {
				client::ModelRenderParam param;
				param.matrix = Matrix4::Translate(MakeVector3(float(edit.placement.anchor.x),
				                                             float(edit.placement.anchor.y),
				                                             float(edit.placement.anchor.z)) +
				                                 host.DocumentOrigin());
				renderer->RenderModel(*placementModel, param);
			}
			DrawMirrorPlanes();
			DrawSelection();
			if (VoxelTool* tool = ActiveTool())
				tool->DrawScene(*this);
			// Voxels waiting to be placed, drawn in their own colours.
			if (edit.placing)
				DrawPlacementPreview();
		}

		std::string VoxelEditor::ToolHintLine() {
			std::string line;
			if (SamplingArmed()) {
				line = "Pick colour:  click a voxel";
			} else if (VoxelTool* tool = ActiveTool()) {
				// A tool whose only sub-tool is itself goes by its own name alone,
				// and a strip by the name of the tool it is the strip of: the one
				// lit on the toolbar.
				const int shownAs = ToolIndex(tool->StripOf());
				line = shownAs >= 0 ? tools[static_cast<size_t>(shownAs)].tool->Label()
				                    : tool->Label();
				if (tool->SubToolCount() > 1)
					line += std::string(" \xE2\x80\xBA ") + tool->SubToolLabel(tool->ActiveSubTool());
				const std::string hint = tool->Hint(*this);
				if (!hint.empty())
					line += ":  " + hint;
			}
			// What Escape does next, from the same list Escape acts on.
			const std::string escape = "[Esc] " + EscapeLabel(NextEscape());
			return line.empty() ? escape : line + kHintSeparator + escape;
		}

		void VoxelEditor::DrawViewportOverlays(float sw, float sh, const std::string& hostHelp) {
			VoxelOverlayLines::Occluders occluders;
			occluders.document = &document;
			occluders.documentVersion = document.Version();
			if (edit.placing && placementVoxels) {
				occluders.pending = placementVoxels.GetPointerOrNull();
				occluders.pendingAnchor = edit.placement.anchor;
				occluders.pendingVersion = placementModelVersion;
			}
			if (currentMode == EditorMode::Edit && !PresentsRender())
				overlayLines.Draw(*renderer, DocumentView(), occluders);

			// The picker is laid out first: the text under the viewport keeps clear of it.
			ColorPicker& picker = *ui.GetColorPicker();
			picker.UpdateLayout(sw, sh, BarsHeight());

			client::IFont& font = fontManager->GetSmallGuiFont();
			const float lineH = 22.0F;
			const float left = 16.0F;

			// The host's keys first, then the editor's, named as they are bound:
			// the help never names a key that does something else. The tools' own
			// keys are on their buttons and in the tool hint, Escape in the hint.
			std::string help = hostHelp;
			auto add = [&](const std::string& part) {
				help += (help.empty() ? std::string() : kHintSeparator) + part;
			};
			if (ModeSupported(EditorMode::Object))
				add(currentMode == EditorMode::Object ? "[Tab] edit the object"
				                                      : "[Tab] back to the objects");
			const bool editing = currentMode == EditorMode::Edit;
			if (editing)
				add("[Alt+LMB] pick colour");
			add("[Ctrl+Z/Y] undo/redo");
			if (editing) {
				add("[Ctrl+C/X/V] copy/cut/paste");
				const std::string deleteKey = cg_keyDelete;
				if (!deleteKey.empty())
					add("[" + deleteKey + "] delete selection");
			}

			// Text stays clear of the colour picker when it is open.
			float right = sw - left;
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

		void VoxelEditor::DrawBars(float sw) {
			DrawToolbar(sw);
			DrawOptionBar(sw);
			DrawContextStrip(sw);
		}

		void VoxelEditor::DrawToolbar(float sw) {
			screenWidth = sw; // for hit tests in ButtonEvent

			// The mode selector, for a document of several modes; one with only
			// one has nothing to switch between, and shows none.
			std::string modeLabel;
			int supported = 0;
			for (const ModeInfo& info : Modes()) {
				if (!ModeSupported(info.mode))
					continue;
				supported++;
				if (info.mode == currentMode)
					modeLabel = std::string(info.label) + " Mode";
			}
			ui.GetToolbar()->SetModeSelector(supported < 2 ? std::string() : modeLabel,
			                                 ModeSupported(EditorMode::Object) ? kModeToggleKey : "",
			                                 ui.GetContextMenu()->IsOpenFor(this));

			// Tool buttons. A hot key the editor already uses for something else
			// never reaches the tool, so the button does not claim it.
			// Each mode shows its own tools: Object mode its own first.
			std::vector<Toolbar::ToolbarButton> toolButtons;
			const bool objectTools = currentMode == EditorMode::Object && ObjectMode();
			if (objectTools) {
				Toolbar::ToolbarButton btn;
				btn.id = kObjectToolId;
				btn.label = objectTool->Label();
				btn.active = objectModeTool < 0;
				toolButtons.push_back(btn);
			}
			const int toolCount =
			  (currentMode == EditorMode::Edit || objectTools) ? int(tools.size()) : 0;
			for (int i = 0; i < toolCount; i++) {
				// A strip has no button: the tool it is the strip of stands for it.
				if (objectTools != InObjectMode(i) || !tools[i].tool->StripOf().empty())
					continue;
				Toolbar::ToolbarButton btn;
				btn.id = tools[i].id;
				btn.label = tools[i].tool->Label();
				btn.hotKey = KeyIsTaken(tools[i].hotKey) ? std::string() : tools[i].hotKey;
				btn.active = objectTools ? (objectModeTool == i)
				                         : (activeTool == i) ||
				                             (activeTool >= 0 && activeTool < toolCount &&
				                              tools[activeTool].tool->StripOf() == tools[i].id);
				btn.enabled = ToolAvailable(i);
				btn.group = tools[i].group;
				toolButtons.push_back(btn);
			}
			ui.GetToolbar()->SetToolButtons(toolButtons);

			// The brush colour is what voxels are painted with: only Edit mode,
			// which edits them, offers it.
			if (currentMode == EditorMode::Edit)
				ui.GetToolbar()->SetColorSwatch(PickerColor(), ui.GetColorPicker()->IsOpen());
			else
				ui.GetToolbar()->HideColorSwatch();
			IObjectModeHost* objects = currentMode == EditorMode::Object ? ObjectMode() : nullptr;
			ui.GetToolbar()->SetUndoButton(objects ? objects->CanUndoObjects() : undo.CanUndo());
			ui.GetToolbar()->SetRedoButton(objects ? objects->CanRedoObjects() : undo.CanRedo());

			ui.GetToolbar()->Draw(*renderer, *fontManager, cursor.GetPosition(),
			                      ui.GetMenuBar()->IsOpen(), sw);
		}

		namespace {
			// A tool's option as the bar draws it.
			OptionBar::Option BarOption(const ToolOption& op) {
				OptionBar::Option opt;
				opt.id = op.id;
				opt.group = op.group;
				opt.label = op.label;
				// A type the bar does not know would be drawn as something the
				// tool never asked for, so each one is named rather than
				// defaulted.
				switch (op.type) {
					case ToolOption::Type::Label: opt.type = OptionBar::OptionType::Label; break;
					case ToolOption::Type::Action: opt.type = OptionBar::OptionType::Action; break;
					case ToolOption::Type::Number: opt.type = OptionBar::OptionType::Number; break;
					case ToolOption::Type::Bool: opt.type = OptionBar::OptionType::Bool; break;
				}
				opt.bvalue = op.bvalue;
				opt.enabled = op.enabled;
				opt.value = op.value;
				opt.step = op.step;
				opt.decimals = op.decimals;
				opt.hex = op.hex;
				return opt;
			}

			void AppendBarOptions(std::vector<OptionBar::Option>& out, const ToolOptions* from) {
				for (int i = 0; from && i < from->Count(); i++) {
					if (from->At(i).visible)
						out.push_back(BarOption(from->At(i)));
				}
			}
		} // namespace

		std::vector<OptionBar::SubToolButton> VoxelEditor::SubToolButtons(VoxelTool& tool) {
			// Lit only while the tool is the one at work: a bar left standing
			// under its strip shows what can be gone back to, not what is on.
			const bool active = &tool == ActiveTool();
			std::vector<OptionBar::SubToolButton> buttons;
			for (int i = 0; i < tool.SubToolCount(); i++) {
				OptionBar::SubToolButton btn;
				btn.label = tool.SubToolLabel(i);
				const std::string key = SubToolHotKey(tool, i);
				btn.hotKey = KeyIsTaken(key) ? std::string() : key;
				btn.group = tool.SubToolGroup(i);
				btn.active = active && tool.ActiveSubTool() == i;
				buttons.push_back(btn);
			}
			return buttons;
		}

		void VoxelEditor::DrawOptionBar(float sw) {
			// With no tool active the bar is drawn empty, so it never keeps
			// answering clicks on buttons that are no longer there.
			VoxelTool* t = BarTool();

			// A single sub-tool is the tool itself, and a lone button that is
			// always on would only be noise.
			std::vector<OptionBar::SubToolButton> subTools;
			if (t && t->SubToolCount() > 1)
				subTools = SubToolButtons(*t);
			std::vector<OptionBar::Option> options;
			if (t) {
				t->UpdateOptions(*this);
				AppendBarOptions(options, t->Options());
			}
			ui.GetOptionBar()->SetContent(t, std::move(subTools), std::move(options));
			ui.GetOptionBar()->Draw(*renderer, *fontManager, cursor.GetPosition(),
			                        ui.GetMenuBar()->IsOpen(), sw);
		}

		void VoxelEditor::DrawContextStrip(float sw) {
			VoxelTool* t = StripTool();
			std::vector<OptionBar::SubToolButton> subTools;
			std::vector<OptionBar::Option> options;
			if (t) {
				subTools = SubToolButtons(*t);
				t->UpdateOptions(*this);
				// Its commands, good at any time, then what the sub-tool at
				// work is set by: nothing while the strip is only on offer.
				AppendBarOptions(options, t->StripCommands());
				if (t == ActiveTool())
					AppendBarOptions(options, t->Options());
			}
			ui.GetContextStrip()->SetContent(t, std::move(subTools), std::move(options));
			ui.GetContextStrip()->Draw(*renderer, *fontManager, cursor.GetPosition(),
			                           ui.GetMenuBar()->IsOpen(), sw);
		}

		void VoxelEditor::DrawPanels() {
			// The transform panel first: the colour picker, when it is open, goes
			// over it.
			if (ITransformTarget* target = PanelTarget()) {
				transformPanel->LayoutAbove(panelRight, panelBottom);
				// The tool owning the target says what its Step is, so Ctrl-dragging
				// a Location moves it by what a drag of the gizmo would.
				if (VoxelTool* tool = ActiveTool())
					transformPanel->SetLocationStep(tool->TransformStep());
				transformPanel->Draw(*target, cursor.GetPosition(), clock);
			}
			ui.GetColorPicker()->Draw(*renderer, *fontManager, cursor.GetPosition());
			if (VoxelTool* tool = ActiveTool())
				tool->DrawOverlay(*this);

		}
	} // namespace gui
} // namespace spades
