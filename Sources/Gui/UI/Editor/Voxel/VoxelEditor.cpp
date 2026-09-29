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

#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IModel.h>
#include <Client/IRenderer.h>
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Core/VoxelModel.h>
#include <Gui/OverlayPaint.h>
#include <Gui/UI/Components/ColorPicker.h>
#include <Gui/UI/Components/EditorMenu.h>
#include <Gui/UI/Components/Gizmo/GizmoCanvas.h>
#include <Gui/UI/Components/Gizmo/TransformGizmo.h>
#include <Gui/UI/Components/OptionBar.h>
#include <Gui/UI/Components/SoftwareCursor.h>
#include <Gui/UI/Components/Toolbar.h>
#include <Gui/UI/Components/UIOverlayHost.h>
#include <Gui/UI/Editor/Shell/EditorKeys.h>
#include <Gui/UI/Editor/Shell/EditorUI.h>

#include "DrawTool.h"
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
		} // namespace

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
			WireUI();
		}

		VoxelEditor::~VoxelEditor() { SPADES_MARK_FUNCTION(); }

		void VoxelEditor::WireUI() {
			// Buttons report ids, resolved against the modes and tools as they are
			// now; the toolbar only reports clicks on enabled buttons.
			ui.GetToolbar()->OnModeClicked = [this](const std::string& id) {
				for (const ModeInfo& info : Modes()) {
					if (id == info.id && ModeSupported(info.mode))
						SetMode(info.mode);
				}
			};
			ui.GetToolbar()->OnToolClicked = [this](const std::string& id) {
				SetActiveTool(ToolIndex(id));
			};
			ui.GetToolbar()->OnUndoClicked = [this]() { Undo(); };
			ui.GetToolbar()->OnRedoClicked = [this]() { Redo(); };

			// The option bar's content belongs to the active tool, so a click only
			// counts while that tool still is.
			ui.GetOptionBar()->CurrentOwner = [this] {
				return static_cast<const void*>(ActiveTool());
			};
			ui.GetOptionBar()->OnSubToolClicked = [this](int index) {
				if (VoxelTool* tool = ActiveTool())
					tool->SetSubTool(*this, index);
			};
			ui.GetOptionBar()->OnBoolToggled = [this](const std::string& id) {
				if (ToolOption* opt = ActiveOption(id, ToolOption::Type::Bool)) {
					opt->bvalue = !opt->bvalue;
					ActiveTool()->OnOptionToggled(*this, id, opt->bvalue);
				}
			};
			ui.GetOptionBar()->OnActionClicked = [this](const std::string& id) {
				if (ActiveOption(id, ToolOption::Type::Action))
					ActiveTool()->OnAction(*this, id);
			};
			ui.GetOptionBar()->OnNumberChanged = [this](const std::string& id, float value,
			                                            bool committed) {
				ToolOption* opt = ActiveOption(id, ToolOption::Type::Number);
				if (!opt)
					return;
				// A settled value is one user action, so a typed number or an
				// arrow press undoes in one go; an unsettled one only previews
				// and has nothing to record.
				if (committed)
					undo.BeginAction();
				UserActionEnd end(*this, committed);
				ActiveTool()->OnOptionNumberChanged(*this, id, value, committed);
			};

			// The brush colour is the editor's, shared by every tool: its swatch
			// sits on the main toolbar and opens or closes the picker, which
			// edits it and stays up across tool switches until closed.
			ui.GetToolbar()->OnColorSwatchClicked = [this] {
				ColorPicker& picker = *ui.GetColorPicker();
				if (picker.IsOpen()) {
					picker.Close();
				} else {
					picker.SetColor(currentColor);
					picker.Open();
				}
			};
			ui.GetColorPicker()->OnColorChanged = [this](std::uint32_t c) { currentColor = c; };
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
			undo.Clear();
			// The mirror planes start on the new document's pivot.
			PlaceMirrorPlane(GetPivot());
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

		// --- Escape -----------------------------------------------------------

		VoxelEditor::EscapeLayer VoxelEditor::NextEscape() {
			if (ui.GetColorPicker()->GetEyedropperMode())
				return EscapeLayer::Eyedropper;
			if (VoxelTool* tool = ActiveTool()) {
				if (!tool->EscapeLabel(*this).empty())
					return EscapeLayer::Tool;
			}
			if (edit.placing)
				return EscapeLayer::Placement;
			if (!edit.selection.Empty())
				return EscapeLayer::Selection;
			return EscapeLayer::None;
		}

		std::string VoxelEditor::EscapeLabel(EscapeLayer layer) {
			switch (layer) {
				case EscapeLayer::Eyedropper: return "stop picking";
				case EscapeLayer::Tool: return ActiveTool()->EscapeLabel(*this);
				case EscapeLayer::Placement: return "drop the pending voxels";
				case EscapeLayer::Selection: return "select nothing";
				case EscapeLayer::None: break;
			}
			return "menu";
		}

		void VoxelEditor::Escape(EscapeLayer layer) {
			switch (layer) {
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
		const Vector2& VoxelEditor::CursorPos() const { return host.AimPosition(); }

		bool VoxelEditor::RayPlaneCell(const Vector3& planePoint, const Vector3& normal,
		                               IntVector3& out) {
			const GizmoView& view = host.CurrentView();
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
			const GizmoView& view = host.CurrentView();
			if (!view.IsValid())
				return;

			Vector3 eye, dir;
			view.Ray(host.AimPosition(), eye, dir);

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
						return; // parallel to this slab and outside it
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
				return; // the ray misses the volume
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
					pickHit = true;
					pickSolid = MakeIntVector3(cx, cy, cz);
					pickPlace = MakeIntVector3(px, py, pz);
					return;
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
			// grid), the selection, and pending voxels, which would otherwise
			// land in the wrong place.
			edit.mirror.plane += shift;
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
				SetStatus(SizeLimitMessage());
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
			return altHeld || ui.GetColorPicker()->GetEyedropperMode();
		}

		bool VoxelEditor::SampleColor() {
			DoPick();
			if (!pickHit)
				return false;
			currentColor = document.Color(pickSolid.x, pickSolid.y, pickSolid.z);
			ColorPicker& picker = *ui.GetColorPicker();
			picker.SetColor(currentColor);
			picker.SetEyedropperMode(false); // a pick is one-shot
			SetStatus("Picked colour");
			return true;
		}

		void VoxelEditor::NoteColorUsed(std::uint32_t color) {
			ui.GetColorPicker()->AddRecentColor(color);
		}

		// --- Selection --------------------------------------------------------

		bool VoxelEditor::IsSelected(int x, int y, int z) const {
			return edit.selection.Contains(MakeIntVector3(x, y, z));
		}

		int VoxelEditor::SelectionCount() const {
			// While a paste or an import waits it is what the selection commands
			// act on.
			return edit.placing ? int(edit.placement.voxels->size()) : edit.selection.Size();
		}

		void VoxelEditor::ClearSelection() {
			DocumentCommand command(*this);
			VoxelUndoStack::Step step(undo, "Clear Selection");
			edit.selection.Clear();
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
				SetStatus(SizeLimitMessage());
				return;
			}
			if (clamped)
				SetStatus(SizeLimitMessage()); // went as far as the limit allows
			// Where the moved voxels land must fit the volume they leave behind,
			// grown to hold them; with every voxel moving, nothing else holds it.
			const VolumeFrame frame =
			  FrameHolding(moved.anchor, moved.anchor + moved.Extent() - MakeIntVector3(1, 1, 1));
			if (!Fits(frame)) {
				SetStatus(SizeLimitMessage());
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
				SetStatus(SizeLimitMessage());
				return;
			}
			if (clamped)
				SetStatus(SizeLimitMessage()); // went as far as the limit allows
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			VoxelUndoStack::Step step(undo, t.Turns() ? "Turn" : "Move");
			edit.placement = std::move(next);
		}

		bool VoxelEditor::TransformPivot(IntVector3& out) const {
			// From what is known already (the waiting voxels' middle, the
			// selection's cached box): it is asked for every frame.
			if (edit.placing) {
				out = TurnCentre(edit.placement.pivot);
				return true;
			}
			IntVector3 middle;
			if (!SelectionMiddleOf(middle))
				return false;
			out = TurnCentre(middle);
			return true;
		}

		IntVector3 VoxelEditor::TurnCentre(const IntVector3& groupMiddle) const {
			if (!turnsAboutModelPivot)
				return groupMiddle;
			// The voxel nearest the pivot; on a tie the lower one, as for the
			// middle of a group.
			const Vector3 p = GetPivot();
			auto nearest = [](float c) { return int(std::ceil(c - 0.5F)); };
			return MakeIntVector3(nearest(p.x), nearest(p.y), nearest(p.z));
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
				SetStatus(SizeLimitMessage());
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

		void VoxelEditor::DrawSelection() {
			const Vector4 col = MakeVector4(1.0F, 0.55F, 0.1F, 0.95F);
			edit.selection.ForEach(
			  [&](const IntVector3& v) { DrawCellOutline(v.x, v.y, v.z, col); });
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
			float lo = -2.5F;
			float hiX = float(stop(w)) - 0.5F;
			float hiY = float(stop(h)) - 0.5F;
			float hiZ = float(stop(d)) - 0.5F;

			if (MirrorEnabled(0)) {
				float px = edit.mirror.plane.x;
				Vector4 col = MakeVector4(1.0F, 0.35F, 0.35F, 0.25F);
				for (int i = -2; i <= stop(h); i += 2)
					renderer->AddDebugLine(MakeVector3(px, float(i) - 0.5F, lo),
					                       MakeVector3(px, float(i) - 0.5F, hiZ), col);
				for (int i = -2; i <= stop(d); i += 2)
					renderer->AddDebugLine(MakeVector3(px, lo, float(i) - 0.5F),
					                       MakeVector3(px, hiY, float(i) - 0.5F), col);
			}
			if (MirrorEnabled(1)) {
				float py = edit.mirror.plane.y;
				Vector4 col = MakeVector4(0.4F, 1.0F, 0.4F, 0.25F);
				for (int i = -2; i <= stop(w); i += 2)
					renderer->AddDebugLine(MakeVector3(float(i) - 0.5F, py, lo),
					                       MakeVector3(float(i) - 0.5F, py, hiZ), col);
				for (int i = -2; i <= stop(d); i += 2)
					renderer->AddDebugLine(MakeVector3(lo, py, float(i) - 0.5F),
					                       MakeVector3(hiX, py, float(i) - 0.5F), col);
			}
			if (MirrorEnabled(2)) {
				float pz = edit.mirror.plane.z;
				Vector4 col = MakeVector4(0.45F, 0.6F, 1.0F, 0.25F);
				for (int i = -2; i <= stop(w); i += 2)
					renderer->AddDebugLine(MakeVector3(float(i) - 0.5F, lo, pz),
					                       MakeVector3(float(i) - 0.5F, hiY, pz), col);
				for (int i = -2; i <= stop(h); i += 2)
					renderer->AddDebugLine(MakeVector3(lo, float(i) - 0.5F, pz),
					                       MakeVector3(hiX, float(i) - 0.5F, pz), col);
			}
		}

		// --- Transform gizmo -------------------------------------------------

		GizmoView VoxelEditor::GetGizmoView() const { return host.CurrentView(); }

		void VoxelEditor::DrawGizmo(const TransformGizmo& gizmo) {
			GizmoCanvas canvas(*renderer);
			gizmo.Draw(canvas, GetGizmoView());
		}

		// --- Tools and modes --------------------------------------------------

		void VoxelEditor::SetActiveTool(int index) {
			if (index >= 0 && index < int(tools.size()))
				Activate(index, currentMode);
		}

		void VoxelEditor::SetMode(EditorMode mode) { Activate(activeTool, mode); }

		void VoxelEditor::Activate(int toolIndex, EditorMode mode) {
			if (toolIndex == activeTool && mode == currentMode)
				return;
			// A number being typed into the sub-toolbar belongs to the tool whose
			// box it is: it is settled here, while that tool is still the one
			// listening. Left to the bar, the edit would be dropped on the next
			// rebuild with whatever it was previewing still showing.
			ui.GetOptionBar()->EndEditing(true);
			// Leaving a tool ends what it had in progress: this is where a pending
			// placement reaches the document.
			if (VoxelTool* previous = ActiveTool())
				previous->OnDeactivate(*this);
			activeTool = toolIndex;
			currentMode = mode;
			if (VoxelTool* current = ActiveTool())
				current->OnActivate(*this);
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

		ToolOption* VoxelEditor::ActiveOption(const std::string& id, ToolOption::Type type) {
			VoxelTool* tool = ActiveTool();
			ToolOptions* options = tool ? tool->Options() : nullptr;
			ToolOption* option = options ? options->Find(id) : nullptr;
			return (option && option->type == type) ? option : nullptr;
		}

		std::string VoxelEditor::SubToolHotKey(int index) {
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
				if (!tools[i].hotKey.empty() && EqualsIgnoringCase(tools[i].hotKey, key)) {
					SetActiveTool(int(i));
					return true;
				}
			}
			VoxelTool* tool = ActiveTool();
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

		VoxelTool* VoxelEditor::ActiveTool() {
			if (currentMode == EditorMode::Edit && activeTool >= 0 && activeTool < int(tools.size()))
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
		}

		void VoxelEditor::CancelToolInteraction() {
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
		}

		bool VoxelEditor::IsEditShortcut(const std::string& key) {
			for (const char* k : {"c", "x", "v", "z", "y"}) {
				if (EqualsIgnoringCase(key, k))
					return true;
			}
			return false;
		}

		void VoxelEditor::RunEditShortcut(const std::string& key) {
			// A shortcut changes the document or the placement under a drag in
			// progress (a paste replaces it), so that drag is abandoned.
			CancelToolInteraction();
			if (EqualsIgnoringCase(key, "c"))
				CopySelection();
			else if (EqualsIgnoringCase(key, "x"))
				CutSelection();
			else if (EqualsIgnoringCase(key, "v"))
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
			const Vector2& at = cursor.GetPosition();
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

			// A release reaches the tool only when its press did: presses the
			// picker, the bars, the host's widgets or colour sampling took are
			// none of the tool's business, and start no user action to end.
			if (button == PointerButton::Left) {
				if (!down) {
					ui.GetColorPicker()->MouseUp();
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

				// The open picker owns every press on its panel, gaps included, so
				// nothing behind it is ever edited through it.
				if (ui.GetColorPicker()->IsOverPicker(at)) {
					ui.GetColorPicker()->MouseDown(at);
					return;
				}

				// Over the bars, a click belongs to whichever button is under it
				// (the callbacks wired in WireUI act on it), or to nothing.
				if (at.y < BarsHeight()) {
					if (!ui.GetToolbar()->Click(at, screenWidth))
						ui.GetOptionBar()->Click(at);
					// Typing into a box means the keyboard is the box's now, so
					// nothing may stay held from before it was clicked.
					if (ui.GetOptionBar()->IsEditing())
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
				if (!down) {
					if (rmbHeld) {
						rmbHeld = false;
						DispatchPointer(MakePointer(PointerButton::Right, PointerPhase::Up));
					}
					return;
				}
				// Not over the colour picker or the bars.
				if (ui.GetColorPicker()->IsOverPicker(at) || at.y < BarsHeight())
					return;
				rmbHeld = true;
				DispatchPointer(MakePointer(PointerButton::Right, PointerPhase::Down));
			}
		}

		bool VoxelEditor::OptionBarKey(const std::string& key) {
			return ui.GetOptionBar()->KeyEvent(key);
		}

		bool VoxelEditor::CommandKey(const std::string& key, bool down) {
			if (down && EditorKeyMatches(cg_keyDelete, key)) {
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
			if (statusTimer > 0.0F)
				statusTimer -= dt;
			RefreshPlacementModel(); // what edits since the last frame left stale
		}

		void VoxelEditor::DrawScene() {
			// Pending voxels (a paste, an import) render solid at their temporary
			// position, in a document that does not hold them yet.
			if (edit.placing && placementModel) {
				client::ModelRenderParam param;
				param.matrix = Matrix4::Translate(MakeVector3(float(edit.placement.anchor.x),
				                                             float(edit.placement.anchor.y),
				                                             float(edit.placement.anchor.z)));
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
				// A tool whose only sub-tool is itself goes by its own name alone.
				line = tool->Label();
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
			overlayLines.Draw(*renderer, host.CurrentView(), occluders);

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
			add("[Alt+LMB] pick colour");
			add("[Ctrl+Z/Y] undo/redo");
			add("[Ctrl+C/X/V] copy/cut/paste");
			const std::string deleteKey = cg_keyDelete;
			if (!deleteKey.empty())
				add("[" + deleteKey + "] delete selection");

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
		}

		void VoxelEditor::DrawToolbar(float sw) {
			screenWidth = sw; // for hit tests in ButtonEvent

			// Mode buttons; the ones not supported yet are greyed out.
			std::vector<Toolbar::ToolbarButton> modeButtons;
			for (const ModeInfo& info : Modes()) {
				Toolbar::ToolbarButton btn;
				btn.id = info.id;
				btn.label = info.label;
				btn.enabled = ModeSupported(info.mode);
				btn.active = info.mode == currentMode;
				modeButtons.push_back(btn);
			}
			ui.GetToolbar()->SetModeButtons(modeButtons);

			// Tool buttons. A hot key the editor already uses for something else
			// never reaches the tool, so the button does not claim it.
			int toolCount = (currentMode == EditorMode::Edit) ? int(tools.size()) : 0;
			std::vector<Toolbar::ToolbarButton> toolButtons;
			for (int i = 0; i < toolCount; i++) {
				Toolbar::ToolbarButton btn;
				btn.id = tools[i].id;
				btn.label = tools[i].tool->Label();
				btn.hotKey = KeyIsTaken(tools[i].hotKey) ? std::string() : tools[i].hotKey;
				btn.active = (activeTool == i);
				btn.group = tools[i].group;
				toolButtons.push_back(btn);
			}
			ui.GetToolbar()->SetToolButtons(toolButtons);

			ui.GetToolbar()->SetColorSwatch(currentColor, ui.GetColorPicker()->IsOpen());
			ui.GetToolbar()->SetUndoButton(undo.CanUndo());
			ui.GetToolbar()->SetRedoButton(undo.CanRedo());

			ui.GetToolbar()->Draw(*renderer, *fontManager, cursor.GetPosition(),
			                      ui.GetEditorMenu()->IsActive(), sw);
		}

		void VoxelEditor::DrawOptionBar(float sw) {
			// With no tool active the bar is drawn empty, so it never keeps
			// answering clicks on buttons that are no longer there.
			VoxelTool* t = ActiveTool();

			// A single sub-tool is the tool itself, and a lone button that is
			// always on would only be noise.
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
			std::vector<OptionBar::Option> options;
			if (t)
				t->UpdateOptions(*this);
			ToolOptions* opts = t ? t->Options() : nullptr;
			if (opts) {
				for (int i = 0; i < opts->Count(); i++) {
					const ToolOption& op = opts->At(i);
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
					options.push_back(opt);
				}
			}
			ui.GetOptionBar()->SetContent(t, std::move(subTools), std::move(options));
			ui.GetOptionBar()->Draw(*renderer, *fontManager, cursor.GetPosition(),
			                        ui.GetEditorMenu()->IsActive(), sw);
		}

		void VoxelEditor::DrawPanels() {
			ui.GetColorPicker()->Draw(*renderer, *fontManager, cursor.GetPosition());
			if (VoxelTool* tool = ActiveTool())
				tool->DrawOverlay(*this);
		}
	} // namespace gui
} // namespace spades
