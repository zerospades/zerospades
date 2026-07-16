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

#include <functional>
#include <string>
#include <vector>

#include <Gui/UI/Widgets/ButtonBase.h>
#include <Gui/UI/Widgets/ListView.h>

namespace spades {
	namespace gui {
		class MainScreenHelper;

		/** Strips the directory and ".dem" suffix from a demo path. */
		std::string StripDemoPath(std::string path);

		/** Parsed demo metadata extracted from a demo filename. */
		struct DemoInfo {
			bool structured = false; // false when the filename doesn't follow the pattern
			std::string displayName; // stripped filename, always set
			std::string timestamp;   // "YYYY-MM-DD HH:MM" — only when structured
			std::string gameMode;    // display-ready mode, e.g. "CTF" — only when structured
			std::string mapName;     // display-ready map name — only when structured
			std::string serverName;  // display-ready server name — only when structured
			std::string fileSize;    // human-readable size, e.g. "1 MB" — always set
		};

		/**
		 * Parses a full demo path (e.g. "Demos/2025-03-14-14-30-myserver-dust-ctf.dem")
		 * into a `DemoInfo`, falling back to displayName-only for non-conforming names.
		 */
		DemoInfo ParseDemoFilename(const std::string& path, MainScreenHelper& helper);

		/** One row of the demo browser. */
		class DemoListItem : public ui::ButtonBase {
			DemoInfo info;
			float colDateWidth;
			float colModeWidth;
			float colMapWidth;
			float colSizeWidth;
			float totalWidth;

		public:
			std::string filename; // full path

			DemoListItem(ui::UIManager* manager, const std::string& filename, DemoInfo info,
			             float colDateWidth, float colModeWidth, float colMapWidth,
			             float colSizeWidth, float totalWidth);
			void Render() override;
		};

		using DemoListItemEventHandler = std::function<void(const std::string& filename)>;

		/** `ListViewModel` for the demo browser. */
		class DemoListModel : public ui::ListViewModel {
			ui::UIManager* manager;   // weak
			MainScreenHelper* helper; // weak
			std::vector<std::string> list;
			std::vector<Handle<DemoListItem>> itemElements;
			float colDateWidth;
			float colModeWidth;
			float colMapWidth;
			float colSizeWidth;
			float totalWidth;

			void OnItemClicked(ui::UIElement& sender);
			void OnItemDoubleClicked(ui::UIElement& sender);

		public:
			DemoListItemEventHandler itemActivated;
			DemoListItemEventHandler itemDoubleClicked;

			DemoListModel(ui::UIManager* manager, MainScreenHelper* helper,
			              std::vector<std::string> list, float colDateWidth, float colModeWidth,
			              float colMapWidth, float colSizeWidth, float totalWidth);

			int GetNumRows() override { return static_cast<int>(list.size()); }
			Handle<ui::UIElement> CreateElement(int row) override;
		};

		/** A static column header for the demo browser. */
		class DemoListHeader : public ui::UIElement {
		public:
			std::string text;

			DemoListHeader(ui::UIManager* manager) : ui::UIElement(manager) {}
			void Render() override;
		};
	} // namespace gui
} // namespace spades
