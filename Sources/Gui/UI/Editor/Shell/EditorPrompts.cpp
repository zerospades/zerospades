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

#include "EditorPrompts.h"

#include <vector>

#include <Gui/UI/Components/UIOverlayHost.h>
#include <Gui/UI/Widgets/MessageBox.h>

namespace spades {
	namespace gui {
		void ShowEditorMessage(UIOverlayHost& overlay, const std::string& text) {
			// An alert rather than a message box with one button: it is the same
			// thing to look at, and it answers to Enter and Escape as every other
			// dismissable box in the game does.
			Handle<AlertScreen> box =
			  Handle<AlertScreen>::New(&overlay.GetUIManager().GetRootElement(), text, 120.0F);
			overlay.Show(box.GetPointerOrNull());
		}

		void ConfirmDiscardChanges(UIOverlayHost& overlay, const std::string& question,
		                           std::function<void(std::function<void()>)> save,
		                           std::function<void()> proceed) {
			Handle<MessageBoxScreen> box = Handle<MessageBoxScreen>::New(
			  &overlay.GetUIManager().GetRootElement(), question,
			  std::vector<std::string>{"Save", "Discard", "Cancel"}, 160.0F);
			box->closed = [save, proceed](ui::UIElement& sender) {
				MessageBoxScreen* answer = dynamic_cast<MessageBoxScreen*>(&sender);
				if (!answer)
					return;
				if (answer->resultIndex == 1) // Discard
					proceed();
				else if (answer->resultIndex == 0) // Save
					save(proceed);
			};
			overlay.Show(box.GetPointerOrNull());
		}
	} // namespace gui
} // namespace spades
