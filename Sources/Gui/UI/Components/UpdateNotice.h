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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <functional>
#include <string>

#include <Gui/UI/Widgets/ButtonBase.h>
#include <Gui/UI/Widgets/MessageBox.h>

namespace spades {
	namespace gui {
		/**
		 * A clickable strip announcing a newer release, which opens the release page.
		 *
		 * It stays in the tree from the start and follows `UpdateChecker` on its own:
		 * until an update is known it draws nothing and ignores the mouse, so owners
		 * add it once and never have to poll the check themselves.
		 *
		 * With `showUpToDate`, a confirmed up-to-date check is shown as a small,
		 * non-interactive mark at the right end of the strip, unless
		 * `upToDateCovered` says another strip covers it.
		 *
		 * `releasePageOpened` runs once the browser has the release page, and is
		 * where the owner closes the application so the update can be installed.
		 */
		class UpdateNotice : public ui::ButtonBase {
			const bool showUpToDate;
			bool upToDate = false;
			std::string releaseUrl;
			std::function<void()> releasePageOpened;

			void Refresh();
			void RenderUpToDate();

		public:
			UpdateNotice(ui::UIManager* manager, std::function<void()> releasePageOpened,
			             bool showUpToDate = false);

			/** Whether another strip drawn over this one hides its up-to-date mark. */
			std::function<bool()> upToDateCovered;

			void OnActivated() override;
			void Render() override;
		};

		/**
		 * The dialog shown once per launch when an update is found, offering to open
		 * the release page, unless turned off with `cl_zsUpdatePrompt`. Escape
		 * dismisses; Enter does nothing, since the prompt can appear mid-keystroke. `releasePageOpened`
		 * runs once the browser has the page, as for `UpdateNotice`.
		 */
		class UpdatePromptScreen : public MessageBoxScreen {
		public:
			/** Shows the prompt over `owner` if the check has an update to announce. */
			static void ShowIfPending(ui::UIElement* owner,
			                          std::function<void()> releasePageOpened);

			UpdatePromptScreen(ui::UIElement* owner, const std::string& text,
			                   const std::string& releaseUrl,
			                   std::function<void()> releasePageOpened);
			~UpdatePromptScreen() override;

			void HotKey(const std::string& key) override;
		};
	} // namespace gui
} // namespace spades
