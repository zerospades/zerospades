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

#pragma once

#include <Gui/UI/Widgets/ButtonBase.h>

namespace spades {
	namespace gui {
		class FileBrowserModel;

		/**
		 * One clickable row of the file browser: a folder or a file.
		 *
		 * Selection state is read from the model at draw time, so a row stays
		 * correct when the list reuses it while scrolling. Over-long names are
		 * shortened with "..", keeping any hint visible.
		 */
		class FileBrowserRow : public ui::ButtonBase {
			FileBrowserModel* model; // weak; the model owns this row
			int row;

		public:
			FileBrowserRow(ui::UIManager* manager, FileBrowserModel* model, int row);

			int GetRow() const { return row; }

			void Render() override;
		};
	} // namespace gui
} // namespace spades
