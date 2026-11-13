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
// VoxelEditor: what it draws: outlines, the gizmo, the scene and the bars.

#include "VoxelEditorInternal.h"

SPADES_SETTING(cg_keyDelete);

namespace spades {
	namespace gui {
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

		// --- The frame's drawing ------------------------------------------------

		// The view is the host's to turn and to make an image of.
		void VoxelEditor::LookIsometric(float elevation) { host.LookIsometric(elevation); }
		void VoxelEditor::TurnView(int quarterTurns) { host.TurnView(quarterTurns); }
		void VoxelEditor::SaveRender(std::uint32_t background, bool transparent) {
			host.SaveRender(background, transparent);
		}

		bool VoxelEditor::PresentsRender() {
			return activeTool >= 0 && activeTool < int(tools.size()) &&
			       tools[static_cast<size_t>(activeTool)].traits.presents;
		}

		void VoxelEditor::DrawScene() {
			// An image of the model holds the model, which the host draws.
			if (PresentsRender())
				return;
			// The images to model from, whatever the tool: they are depth tested,
			// so the voxels in front of them hide them.
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
				const int shownAs = (activeTool >= 0 && activeTool < int(tools.size()))
				                      ? ToolIndex(tools[static_cast<size_t>(activeTool)].traits.stripOf)
				                      : -1;
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
			if (!PresentsRender())
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
			add("[Alt+LMB] pick colour");
			add("[" + CommandShortcut("Z/Y") + "] undo/redo");
			add("[" + CommandShortcut("C/X/V") + "] copy/cut/paste");
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
			DrawContextStrip(sw);
		}

		void VoxelEditor::DrawToolbar(float sw) {
			screenWidth = sw; // for hit tests in ButtonEvent

			// Tool buttons. A hot key the editor already uses for something else
			// never reaches the tool, so the button does not claim it.
			const int toolCount = int(tools.size());
			std::vector<Toolbar::ToolbarButton> toolButtons;
			for (int i = 0; i < toolCount; i++) {
				// A strip has no button: the tool it is the strip of stands for it.
				if (!tools[i].traits.stripOf.empty())
					continue;
				Toolbar::ToolbarButton btn;
				btn.id = tools[i].id;
				btn.label = tools[i].tool->Label();
				btn.hotKey = KeyIsTaken(tools[i].hotKey) ? std::string() : tools[i].hotKey;
				btn.active = (activeTool == i) ||
				             (activeTool >= 0 && activeTool < toolCount &&
				              tools[activeTool].traits.stripOf == tools[i].id);
				btn.enabled = ToolAvailable(i);
				btn.group = tools[i].group;
				toolButtons.push_back(btn);
			}
			ui.GetToolbar()->SetToolButtons(toolButtons);

			ui.GetToolbar()->SetColorSwatch(PickedColor(), ui.GetColorPicker()->IsOpen());
			ui.GetToolbar()->SetUndoButton(undo.CanUndo());
			ui.GetToolbar()->SetRedoButton(undo.CanRedo());

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
					case ToolOption::Type::Color: break; // not shown: see AppendBarOptions
				}
				opt.bvalue = op.bvalue;
				opt.enabled = op.enabled;
				opt.value = op.value;
				opt.step = op.step;
				opt.decimals = op.decimals;
				opt.hex = op.hex;
				return opt;
			}

			// `from`'s options as a bar shows them: the visible ones, of them
			// only the standing ones where `standingOnly`, and never a colour,
			// which the toolbar's swatch stands for.
			void AppendBarOptions(std::vector<OptionBar::Option>& out, const ToolOptions* from,
			                      bool standingOnly = false) {
				for (int i = 0; from && i < from->Count(); i++) {
					const ToolOption& op = from->At(i);
					if (!op.visible || op.type == ToolOption::Type::Color ||
					    (standingOnly && !op.standing))
						continue;
					out.push_back(BarOption(op));
				}
			}
		} // namespace

		std::string VoxelEditor::ShownSubToolKey(VoxelTool& tool, int index) {
			// A key the editor already uses for something else never reaches
			// the sub-tool, so its button does not claim it.
			const std::string key = SubToolHotKey(tool, index);
			return KeyIsTaken(key) ? std::string() : key;
		}

		namespace {
			// A tool's sub-tools as a bar's buttons, each with the key
			// `keyOf` gives it. Lit only while the tool is the one at work
			// (`atWork`): a bar left standing under its strip shows what can be
			// gone back to, not what is on.
			std::vector<OptionBar::SubToolButton>
			SubToolButtons(VoxelTool& tool, bool atWork,
			               const std::function<std::string(int)>& keyOf) {
				std::vector<OptionBar::SubToolButton> buttons;
				for (int i = 0; i < tool.SubToolCount(); i++) {
					OptionBar::SubToolButton btn;
					btn.label = tool.SubToolLabel(i);
					btn.hotKey = keyOf(i);
					btn.group = tool.SubToolGroup(i);
					btn.active = atWork && tool.ActiveSubTool() == i;
					buttons.push_back(btn);
				}
				return buttons;
			}
		} // namespace

		void VoxelEditor::DrawOptionBar(float sw) {
			// With no tool active the bar is drawn empty, so it never keeps
			// answering clicks on buttons that are no longer there.
			VoxelTool* t = BarTool();

			// A single sub-tool is the tool itself, and a lone button that is
			// always on would only be noise.
			std::vector<OptionBar::SubToolButton> subTools;
			if (t && t->SubToolCount() > 1)
				subTools = SubToolButtons(*t, t == ActiveTool(),
				                          [&](int i) { return ShownSubToolKey(*t, i); });
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
				subTools = SubToolButtons(*t, t == ActiveTool(),
				                          [&](int i) { return ShownSubToolKey(*t, i); });
				t->UpdateOptions(*this);
				// Its standing commands, good at any time, and while it is at
				// work what its sub-tool is set by as well.
				AppendBarOptions(options, t->Options(), t != ActiveTool());
			}
			ui.GetContextStrip()->SetContent(t, std::move(subTools), std::move(options));
			ui.GetContextStrip()->Draw(*renderer, *fontManager, cursor.GetPosition(),
			                           ui.GetMenuBar()->IsOpen(), host.ViewTopRightLimit(sw));
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
