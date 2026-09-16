/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <string>

#include <Gui/UI/Components/FileBrowser/FileBrowserView.h>
#include <Gui/UI/KV6Editor/KV6ScreenHelper.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Label.h>

namespace spades {
	namespace gui {
		class MainScreenHelper;

		/** A modal dialog to select model type: KV6 (enabled) or Map VXL (disabled). */
		class KV6ModelTypePrompt : public ui::UIElement {
			ui::UIElement* owner;   // weak
			ui::Button* vxlButton;  // weak; owned as a child

			void OnKV6(ui::UIElement& sender);
			void OnVXL(ui::UIElement& sender);
			void OnCancel(ui::UIElement& sender);

		public:
			ui::EventHandler closed;
			int result = -1; // -1: cancelled, 0: KV6, 1: VXL

			KV6ModelTypePrompt(ui::UIElement* owner);

			void Close();
			void Run();
			void HotKey(const std::string& key) override;
		};

		/**
		 * The KV6 editor's entry point in the main menu: a `FileBrowserView` over
		 * the whole filesystem that launches the editor on the chosen model.
		 *
		 * Everything browser-shaped lives in the shared component, so this panel
		 * only supplies the model file types, the "New" action, and what to do with
		 * the path that comes back.
		 */
		class KV6BrowserPanel : public ui::UIElement {
			MainScreenHelper* helper;  // weak; provides OpenKV6Editor
			ui::UIElement* modalOwner; // weak; modal dialogs disable and cover it
			Handle<KV6ScreenHelper> fs;
			FileBrowserView* browser; // weak; owned as a child

			void OpenModel(const std::string& absPath, bool isNew);
			void OnEntryRejected(const FileBrowserEntry& entry);
			void OnNewModel();
			void OnNewModelTypeClosed(ui::UIElement& sender);
			void OnNewModelNameClosed(ui::UIElement& sender);

			/** Error message if `name` cannot be created in the browsed folder. */
			std::string ValidateNewName(const std::string& name) const;
			/** `name` with the .kv6 extension appended if it lacks one. */
			static std::string ModelFileName(const std::string& name);

		public:
			KV6BrowserPanel(ui::UIManager* manager, MainScreenHelper* helper,
			                ui::UIElement* modalOwner, float contentsLeft, float contentsWidth,
			                float headerPos, float headerHeight, float listPos, float footerPos);

			/** React to Enter: open the selected model, or the typed path. */
			void SubmitDefault();

			/** Rebuild the listing; call when the tab becomes visible. */
			void Refresh();
		};
	} // namespace gui
} // namespace spades
