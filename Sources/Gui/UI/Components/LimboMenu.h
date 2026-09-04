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

#include <vector>
#include "ILimboMenuHost.h"
#include "IModalMenu.h"
#include "MenuItem.h"
#include <Client/GameConstants.h>
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class IRenderer;
		class FontManager;
	} // namespace client
	namespace gui {
		class SoftwareCursor;

		class LimboMenu : public IModalMenu {
		public:
			LimboMenu(Handle<ILimboMenuHost> host, client::IRenderer& renderer,
					  client::FontManager& fontManager, SoftwareCursor& cursor);
			~LimboMenu();

			// IModalMenu implementation
			bool IsActive() const override { return isActive; }
			void Open() override;
			void Close() override;
			bool KeyEvent(const std::string& key, bool down) override;
			void TextInputEvent(const std::string& text) override;
			bool AcceptsTextInput() const override { return false; }
			void Draw() override;

			// Input handlers
			void Update(float dt);
			void MouseEvent(float x, float y);

		private:
			Handle<ILimboMenuHost> host;
			Handle<client::IRenderer> renderer;
			Handle<client::FontManager> fontManager;
			SoftwareCursor& cursor;

			bool isActive = false;
			int prevSelectedItem = -1;

			std::vector<MenuItem> items;

			struct CachedLayout {
				float screenWidth = 0.0F;
				float screenHeight = 0.0F;
				float contentsWidth = 0.0F;
				float left = 0.0F;
				float top = 0.0F;
				float menuWidth = 200.0F;
				float menuHeight = 25.0F;
				float rowHeight = 28.0F;
				float teamX = 0.0F;
				float weapX = 0.0F;
				float firstY = 0.0F;
				bool isValid = false;
			} cachedLayout;

			void RecalculateLayout();
			void RecalculateItemRects();
			/** Recomputes layout and rects if the screen extent moved since last time. */
			void EnsureLayout();
			void UpdateItemVisibility();
			void UpdateHover();
			int MenuButtonAt(const Vector2& p) const;
			void HandleMenuItemSelection(MenuItemType type);
		};
	} // namespace gui
} // namespace spades
