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

#include <Gui/DocumentTypes.h>
#include <Gui/UI/Components/FileBrowser/FileBrowserView.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Label.h>

namespace spades {
	namespace gui {
		class MainScreenHelper;

		/**
		 * A modal asking which kind of document to create: one button per
		 * document type (see DocumentTypes), the ones no editor opens yet
		 * greyed out.
		 */
		class NewDocumentPrompt : public ui::UIElement {
			ui::UIElement* owner; // weak

			void Choose(const DocumentType& type);

		public:
			ui::EventHandler closed;
			/** The type picked; null when the prompt was cancelled. */
			const DocumentType* result = nullptr;

			NewDocumentPrompt(ui::UIElement* owner);

			void Close();
			void Run();
			void HotKey(const std::string& key) override;
		};

		/**
		 * The editors' entry point in the main menu: a `FileBrowserView` over the
		 * whole filesystem that opens the chosen file in its editor.
		 *
		 * Everything browser-shaped lives in the shared component, so this panel
		 * only supplies the "New" action and what to do with the path that comes
		 * back.
		 */
		class EditorBrowserPanel : public ui::UIElement {
			MainScreenHelper* helper;  // weak; provides OpenEditor
			ui::UIElement* modalOwner; // weak; modal dialogs disable and cover it
			FileBrowserView* browser;  // weak; owned as a child
			// The kind the "New" prompt picked, while its name is being asked for.
			DocumentKind newKind = DocumentKind::Model;

			void OpenDocument(const std::string& absPath, bool isNew);
			void OnEntryRejected(const FileBrowserEntry& entry);
			void OnNew();
			void OnNewTypeClosed(ui::UIElement& sender);
			void OnNewNameClosed(ui::UIElement& sender);

			/** Error message if `name` cannot be created in the browsed folder. */
			std::string ValidateNewName(const std::string& name) const;

		public:
			EditorBrowserPanel(ui::UIManager* manager, MainScreenHelper* helper,
			                   ui::UIElement* modalOwner, float contentsLeft, float contentsWidth,
			                   float headerPos, float headerHeight, float listPos, float footerPos);

			/** React to Enter: open the selected file, or the typed path. */
			void SubmitDefault();

			/** Rebuild the listing; call when the tab becomes visible. */
			void Refresh();
		};
	} // namespace gui
} // namespace spades
