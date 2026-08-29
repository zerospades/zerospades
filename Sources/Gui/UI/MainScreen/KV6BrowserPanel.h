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

#include <Gui/UI/KV6Editor/KV6ScreenHelper.h>
#include <Gui/UI/MainScreen/EditorListView.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Field.h>
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

		/** A modal text prompt (title, editable field, OK/Cancel) used for names. */
		class KV6NamePrompt : public ui::UIElement {
			ui::UIElement* owner;   // weak
			ui::Field* nameField;   // weak; owned as a child

			void OnConfirm(ui::UIElement& sender);
			void OnCancel(ui::UIElement& sender);

		public:
			ui::EventHandler closed;
			bool result = false;
			std::string text;

			KV6NamePrompt(ui::UIElement* owner, const std::string& title,
			              const std::string& initial);

			void Close();
			void Run();
			void HotKey(const std::string& key) override;
		};

		/**
		 * The KV6 editor's entry point: a filesystem explorer tab that browses
		 * folders and voxel model files and launches the editor on the chosen one.
		 *
		 * A self-contained panel so the main menu only has to place it in the tab
		 * strip; all browser state and actions live here rather than in the menu.
		 */
		class KV6BrowserPanel : public ui::UIElement {
			MainScreenHelper* helper;  // weak; provides OpenKV6Editor
			ui::UIElement* modalOwner; // weak; modal dialogs disable and cover it
			Handle<KV6ScreenHelper> fs;

			ui::Field* pathField; // weak; owned as a child
			ui::ListView* list;   // weak; owned as a child
			Handle<EditorListModel> currentModel;

			std::string dir;              // current folder (absolute)
			std::string selected;         // selected entry name within `dir`
			bool selectedIsFolder = false;

			std::string Child(const std::string& name) const;
			void Reload();
			void OpenModel(const std::string& absPath, bool isNew);
			void NotImplemented();

			void OnItemActivated(const std::string& name, bool isFolder);
			void OnItemDoubleClicked(const std::string& name, bool isFolder);
			void OnHome(ui::UIElement& sender);
			void OnUp(ui::UIElement& sender);
			void OnNewFolder(ui::UIElement& sender);
			void OnNewFolderClosed(ui::UIElement& sender);
			void OnNewModel(ui::UIElement& sender);
			void OnNewModelTypeClosed(ui::UIElement& sender);
			void OnNewModelNameClosed(ui::UIElement& sender);
			void OnDelete(ui::UIElement& sender);
			void OnDeleteClosed(ui::UIElement& sender);

		public:
			KV6BrowserPanel(ui::UIManager* manager, MainScreenHelper* helper,
			                ui::UIElement* modalOwner, float contentsLeft, float contentsWidth,
			                float headerPos, float headerHeight, float listPos, float footerPos);

			/** Navigate to / open the absolute path typed in the field (on Enter). */
			void SubmitPath();

			/** Rebuild the listing; call when the tab becomes visible. */
			void Refresh() { Reload(); }
		};
	} // namespace gui
} // namespace spades
