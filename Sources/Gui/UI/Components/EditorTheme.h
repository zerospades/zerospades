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

#include <Core/Math.h>

namespace spades {
	namespace client {
		class IRenderer;
	} // namespace client
	namespace gui {
		/**
		 * The colours the editors' own chrome is drawn in, beside the widgets
		 * the game's UI shares (UIWidgetPainter): the floating panels and menus
		 * over the view, the text on them, and the marks they make. Every one
		 * of them asks here, so they read as one set and change as one.
		 * Colours are not alpha premultiplied.
		 */
		namespace EditorTheme {
			// Floating panels and menus, over the view.
			Vector4 PanelFill();
			Vector4 PanelBorder();
			Vector4 PanelShadow();
			Vector4 Separator();

			// Text on them.
			Vector4 Text();
			Vector4 TextDim();   // labels, what is secondary
			Vector4 TextFaint(); // hints, what is disabled
			Vector4 TextShadow();

			// Marks.
			Vector4 Highlight(); // the item under the pointer, chosen with the keys
			Vector4 Refused();   // what cannot be done, and why
			Vector4 DropTarget(); // where something dragged would land

			// A text being typed.
			Vector4 FieldFill();
			Vector4 TextSelection();
			Vector4 Caret();

			// The picked object and the rest of the selection, as the view
			// outlines them and the tree lists them.
			Vector4 Active();
			Vector4 Selected();

			/** A floating panel's body: its shadow, its fill and its border. */
			void PaintPanel(client::IRenderer& renderer, float x, float y, float width,
			                float height);
		} // namespace EditorTheme
	} // namespace gui
} // namespace spades
