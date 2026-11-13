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
// VoxelEditor: the pointer and the keys.

#include "VoxelEditorInternal.h"

SPADES_SETTING(cg_keyDelete);

namespace spades {
	namespace gui {
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

		bool VoxelEditor::IsCommandHeld() const {
			return ctrlHeld || (MetaIsCommandModifier() && metaHeld);
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
			if (EqualsIgnoringCase(key, "a")) {
				// Select All, and with Shift the other way about, as a list in
				// any application does it.
				if (shiftHeld)
					ClearSelection();
				else
					SelectAll();
			} else if (EqualsIgnoringCase(key, "i"))
				InvertSelection();
			else if (EqualsIgnoringCase(key, "c"))
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

		bool VoxelEditor::WheelOverBars(float delta) {
			const Vector2& at = cursor.GetPosition();
			return ui.GetOptionBar()->Wheel(at, delta) || ui.GetContextStrip()->Wheel(at, delta);
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
			if (down && !IsCommandHeld() && !altHeld && SwitchByHotKey(key))
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
	} // namespace gui
} // namespace spades
