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

#include "LimboMenu.h"
#include "SoftwareCursor.h"
#include <Client/IRenderer.h>
#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Core/Strings.h>

namespace spades {
	namespace gui {
		namespace {
			/** Team id a team row stands for; matches `LimboView`'s 0/1/2 encoding. */
			int TeamIdOf(MenuItemType type) {
				switch (type) {
					case MenuItemType::Team1: return 0;
					case MenuItemType::Team2: return 1;
					default: return 2;
				}
			}
			WeaponType WeaponOf(MenuItemType type) {
				switch (type) {
					case MenuItemType::WeaponRifle: return RIFLE_WEAPON;
					case MenuItemType::WeaponSMG: return SMG_WEAPON;
					default: return SHOTGUN_WEAPON;
				}
			}
		} // namespace

		LimboMenu::LimboMenu(Handle<ILimboMenuHost> host, client::IRenderer& renderer,
							 client::FontManager& fontManager, SoftwareCursor& cursor)
		    : host(host), renderer(&renderer), fontManager(&fontManager), cursor(cursor) {}

		LimboMenu::~LimboMenu() {}

		void LimboMenu::Open() {
			if (!host)
				return;

			prevSelectedItem = -1;
			items.clear();

			RecalculateLayout();

			const AABB2 empty = AABB2(0.0F, 0.0F, 0.0F, 0.0F);
			items.push_back(MenuItem(MenuItemType::Team1, empty, host->GetTeamName(0)));
			items.push_back(MenuItem(MenuItemType::Team2, empty, host->GetTeamName(1)));
			items.push_back(MenuItem(MenuItemType::TeamSpectator, empty, _Tr("Client", "Spectator")));
			items.push_back(MenuItem(MenuItemType::WeaponRifle, empty, _Tr("Client", "Rifle")));
			items.push_back(MenuItem(MenuItemType::WeaponSMG, empty, _Tr("Client", "SMG")));
			items.push_back(MenuItem(MenuItemType::WeaponShotgun, empty, _Tr("Client", "Shotgun")));
			items.push_back(MenuItem(MenuItemType::Spawn, empty, _Tr("Client", "Spawn")));
			items.push_back(MenuItem(MenuItemType::Close, empty, "X"));

			// Rects, visibility and hover are always derived, never duplicated here:
			// the same passes that keep the menu right every frame also seed it, so
			// the frame it opens on draws exactly what any later frame would.
			RecalculateItemRects();
			UpdateItemVisibility();
			UpdateHover();

			// Last, so the menu is never observably active while half-built.
			isActive = true;
		}

		void LimboMenu::Close() {
			isActive = false;
			items.clear();
		}

		void LimboMenu::RecalculateLayout() {
			cachedLayout.screenWidth = renderer->ScreenWidth();
			cachedLayout.screenHeight = renderer->ScreenHeight();

			cachedLayout.menuWidth = 200.0F;
			cachedLayout.menuHeight = cachedLayout.menuWidth / 8.0F;
			cachedLayout.rowHeight = cachedLayout.menuHeight + 3.0F;

			cachedLayout.contentsWidth = cachedLayout.screenWidth - 8.0F;
			float maxContentsWidth = 800.0F;
			if (cachedLayout.contentsWidth > maxContentsWidth)
				cachedLayout.contentsWidth = maxContentsWidth;

			cachedLayout.left = (cachedLayout.screenWidth - cachedLayout.contentsWidth) * 0.5F;
			cachedLayout.top = cachedLayout.screenHeight - 150.0F;

			cachedLayout.teamX = cachedLayout.left + 10.0F;
			cachedLayout.weapX = cachedLayout.left + 260.0F;
			cachedLayout.firstY = cachedLayout.top + 35.0F;

			cachedLayout.isValid = true;
		}

		void LimboMenu::EnsureLayout() {
			if (cachedLayout.isValid && cachedLayout.screenWidth == renderer->ScreenWidth() &&
				cachedLayout.screenHeight == renderer->ScreenHeight())
				return;

			RecalculateLayout();
			RecalculateItemRects();
		}

		void LimboMenu::UpdateItemVisibility() {
			const int selectedTeam = host->GetSelectedTeam();
			for (MenuItem& item : items) {
				switch (item.type) {
					case MenuItemType::WeaponRifle:
					case MenuItemType::WeaponSMG:
					case MenuItemType::WeaponShotgun:
						// No weapon to pick while spectating.
						item.visible = selectedTeam < 2;
						break;
					case MenuItemType::Close:
						// Nothing to go back to until the first spawn.
						item.visible = host->HasLocalPlayer();
						break;
					default: item.visible = true;
				}
			}
		}

		void LimboMenu::RecalculateItemRects() {
			for (size_t i = 0; i < items.size(); i++) {
				MenuItem& item = items[i];
				switch (item.type) {
					case MenuItemType::Team1:
						item.rect = AABB2(cachedLayout.teamX, cachedLayout.firstY, cachedLayout.menuWidth, cachedLayout.menuHeight);
						break;
					case MenuItemType::Team2:
						item.rect = AABB2(cachedLayout.teamX, cachedLayout.firstY + cachedLayout.rowHeight, cachedLayout.menuWidth, cachedLayout.menuHeight);
						break;
					case MenuItemType::TeamSpectator:
						item.rect = AABB2(cachedLayout.teamX, cachedLayout.firstY + cachedLayout.rowHeight * 2.0F, cachedLayout.menuWidth, cachedLayout.menuHeight);
						break;
					case MenuItemType::WeaponRifle:
						item.rect = AABB2(cachedLayout.weapX, cachedLayout.firstY, cachedLayout.menuWidth, cachedLayout.menuHeight);
						break;
					case MenuItemType::WeaponSMG:
						item.rect = AABB2(cachedLayout.weapX, cachedLayout.firstY + cachedLayout.rowHeight, cachedLayout.menuWidth, cachedLayout.menuHeight);
						break;
					case MenuItemType::WeaponShotgun:
						item.rect = AABB2(cachedLayout.weapX, cachedLayout.firstY + cachedLayout.rowHeight * 2.0F, cachedLayout.menuWidth, cachedLayout.menuHeight);
						break;
					case MenuItemType::Spawn:
						item.rect = AABB2(cachedLayout.left + cachedLayout.contentsWidth - 166.0F, cachedLayout.firstY + 4.0F, 156.0F, 64.0F);
						break;
					case MenuItemType::Close:
						item.rect = AABB2(cachedLayout.left + cachedLayout.contentsWidth - 24.0F, cachedLayout.top, 24.0F, 24.0F);
						break;
				}
			}
		}

		bool LimboMenu::KeyEvent(const std::string& key, bool down) {
			if (!isActive)
				return false;

			if (!down)
				return false;

			if (key == "LeftMouseButton") {
				// Clicks arrive during event dispatch, ahead of the frame's Update, so
				// a resize or an earlier click in the same batch could otherwise be
				// hit-tested against rects and visibility that no longer hold.
				EnsureLayout();
				UpdateItemVisibility();

				int idx = MenuButtonAt(cursor.GetPosition());
				if (idx >= 0 && idx < static_cast<int>(items.size())) {
					host->PlaySelectSound();
					HandleMenuItemSelection(items[idx].type);
				}
				return true;
			}

			// Keyboard shortcuts
			if (key == "1") {
				if (host->GetSelectedTeam() >= 2) {
					host->OnTeamSelected(0);
				} else {
					host->OnWeaponSelected(RIFLE_WEAPON);
					host->OnSpawnPressed();
				}
				host->PlaySelectSound();
				return true;
			} else if (key == "2") {
				if (host->GetSelectedTeam() >= 2) {
					host->OnTeamSelected(1);
				} else {
					host->OnWeaponSelected(SMG_WEAPON);
					host->OnSpawnPressed();
				}
				host->PlaySelectSound();
				return true;
			} else if (key == "3") {
				if (host->GetSelectedTeam() < 2) {
					host->OnWeaponSelected(SHOTGUN_WEAPON);
				}
				host->PlaySelectSound();
				host->OnSpawnPressed();
				return true;
			} else if (key == "Return" || key == "Enter") {
				host->PlaySelectSound();
				host->OnSpawnPressed();
				return true;
			} else if (key == "Escape") {
				// Unreachable today: Client_Input.cpp handles Escape before dispatch
				// reaches here. Kept because dismissing on Escape is this menu's own
				// contract, not something it should depend on a caller to provide.
				host->PlaySelectSound();
				host->OnClosePressed();
				return true;
			}

			return false;
		}

		void LimboMenu::TextInputEvent(const std::string& text) {}

		void LimboMenu::Draw() {
			if (!isActive)
				return;

			EnsureLayout();

			client::IFont& font = fontManager->GetGuiFont();

			const float left = cachedLayout.left;
			const float top = cachedLayout.top;
			const float contentsWidth = cachedLayout.contentsWidth;
			const float height = 140.0F;

			const int selectedTeam = host->GetSelectedTeam();
			const WeaponType selectedWeapon = host->GetSelectedWeapon();

			// draw background
			renderer->SetColorAlphaPremultiplied(MakeVector4(0.0F, 0.0F, 0.0F, 0.5F));
			renderer->DrawFilledRect(left, top, left + contentsWidth, top + height);

			Vector4 color = MakeVector4(1, 1, 1, 1);
			Vector4 shadowColor = MakeVector4(0, 0, 0, 0.4F);
			Vector4 hotkeyColor = MakeVector4(1, 1, 1, 0.6F);

			{
				auto str = _Tr("Client", "Select Team:");
				Vector2 pos = {left + 10.0F, top + 10.0F};
				font.DrawShadow(str, pos, 1.0F, color, shadowColor);
			}

			if (selectedTeam < 2) {
				auto str = _Tr("Client", "Select Weapon:");
				Vector2 pos = {cachedLayout.weapX, top + 10.0F};
				font.DrawShadow(str, pos, 1.0F, color, shadowColor);
			}

			for (const auto& item : items) {
				if (!item.visible)
					continue;

				bool selected = false;
				int index = 0;
				switch (item.type) {
					case MenuItemType::Team1:
					case MenuItemType::Team2:
					case MenuItemType::TeamSpectator: {
						int teamId = TeamIdOf(item.type);
						selected = (selectedTeam == teamId);
						// Only while spectating do 1/2/3 pick a team; on a team they
						// pick a weapon, so the hint moves to the weapon column.
						index = (selectedTeam >= 2) ? (1 + teamId) : 0;
						break;
					}
					case MenuItemType::WeaponRifle:
					case MenuItemType::WeaponSMG:
					case MenuItemType::WeaponShotgun: {
						WeaponType weapon = WeaponOf(item.type);
						selected = (selectedWeapon == weapon);
						index = (selectedTeam < 2) ? (1 + (int)weapon) : 0;
						break;
					}
					default: selected = false;
				}

				Vector4 fillColor = MakeVector4(0.2F, 0.2F, 0.2F, 0.5F);
				if (selected)
					fillColor = MakeVector4(0.7F, 0.7F, 0.7F, 1) * 0.9F;
				else if (item.hover)
					fillColor = MakeVector4(0.4F, 0.4F, 0.4F, 1) * 0.7F;

				renderer->SetColorAlphaPremultiplied(fillColor);
				renderer->DrawImage(nullptr, item.rect);

				renderer->SetColorAlphaPremultiplied(fillColor * 0.8F);
				renderer->DrawOutlinedRect(item.rect.GetMinX(), item.rect.GetMinY(),
										   item.rect.GetMaxX(), item.rect.GetMaxY());

				if (item.type == MenuItemType::Spawn || item.type == MenuItemType::Close) {
					Vector2 size = font.Measure(item.text);
					Vector2 pos = item.rect.min;
					pos.x += (item.rect.GetWidth() - size.x) * 0.5F;
					pos.y += (item.rect.GetHeight() - size.y) * 0.5F;

					if (item.type == MenuItemType::Close) {
						font.DrawShadow(item.text, pos, 1.0F, color, shadowColor);

						// draw hotkey hint
						std::string hotKeyText = _Tr("Client", "[Esc]");
						Vector2 hotKeySize = font.Measure(hotKeyText);
						Vector2 hotKeyPos = {item.rect.GetMinX() - hotKeySize.x - 5.0F, pos.y};
						font.DrawShadow(hotKeyText, hotKeyPos, 1.0F, hotkeyColor, shadowColor);
					} else {
						Vector2 padPos = item.rect.min + MakeVector2(8.0F, 8.0F);
						Vector2 padSize = MakeVector2(item.rect.GetWidth() - 16.0F,
													  item.rect.GetHeight() - 16.0F);
						Vector2 txtSize = font.Measure(item.text);
						Vector2 txtPos = {padPos.x, padPos.y + (padSize.y - txtSize.y) * 0.5F};
						font.DrawShadow(item.text, txtPos, 1.0F, color, shadowColor);

						// draw hotkey hint
						std::string hotKeyText = _Tr("Client", "[1, 2, 3]");
						Vector2 hotKeySize = font.Measure(hotKeyText);
						Vector2 hotKeyPos = {padPos.x + padSize.x - hotKeySize.x,
											 padPos.y + (padSize.y - hotKeySize.y) * 0.5F};
						font.DrawShadow(hotKeyText, hotKeyPos, 1.0F, hotkeyColor, shadowColor);
					}
				} else {
					std::string str = item.text;
					if (item.type == MenuItemType::Team1)
						str = host->GetTeamName(0);
					else if (item.type == MenuItemType::Team2)
						str = host->GetTeamName(1);

					Vector2 size = font.Measure(str);
					Vector2 pos = item.rect.min;
					pos.x += 5.0F;
					pos.y += (item.rect.GetHeight() - size.y) * 0.5F;
					font.DrawShadow(str, pos, 1.0F, color, shadowColor);

					// draw hotkey hint
					if (index > 0) {
						str = Format("[{0}]", index);
						pos.x = (item.rect.GetMaxX() - 5.0F) - font.Measure(str).x;
						font.DrawShadow(str, pos, 1.0F, hotkeyColor, shadowColor);
					}
				}
			}

			// draw cursor
			cursor.Draw();
		}

		void LimboMenu::Update(float dt) {
			if (!isActive)
				return;

			// Hit-testing has to see the rects and visibility this frame will draw.
			EnsureLayout();
			UpdateItemVisibility();
			UpdateHover();
		}

		void LimboMenu::UpdateHover() {
			for (MenuItem& item : items)
				item.hover = false;

			int hoveredIndex = MenuButtonAt(cursor.GetPosition());
			if (hoveredIndex < 0) {
				prevSelectedItem = -1;
				return;
			}

			items[hoveredIndex].hover = true;
			if (hoveredIndex != prevSelectedItem) {
				host->PlayHoverSound();
				prevSelectedItem = hoveredIndex;
			}
		}

		void LimboMenu::MouseEvent(float x, float y) {
			if (isActive)
				cursor.SetPosition(MakeVector2(x, y));
		}

		int LimboMenu::MenuButtonAt(const Vector2& p) const {
			for (size_t i = 0; i < items.size(); i++) {
				const MenuItem& item = items[i];
				if (!item.visible)
					continue;
				if (item.rect && p) // half-open, as everywhere else in the UI
					return static_cast<int>(i);
			}
			return -1;
		}

		void LimboMenu::HandleMenuItemSelection(MenuItemType type) {
			switch (type) {
				case MenuItemType::Team1:
					host->OnTeamSelected(0);
					break;
				case MenuItemType::Team2:
					host->OnTeamSelected(1);
					break;
				case MenuItemType::TeamSpectator:
					host->OnTeamSelected(2);
					break;
				case MenuItemType::WeaponRifle:
					host->OnWeaponSelected(RIFLE_WEAPON);
					break;
				case MenuItemType::WeaponSMG:
					host->OnWeaponSelected(SMG_WEAPON);
					break;
				case MenuItemType::WeaponShotgun:
					host->OnWeaponSelected(SHOTGUN_WEAPON);
					break;
				case MenuItemType::Spawn:
					host->OnSpawnPressed();
					break;
				case MenuItemType::Close:
					host->OnClosePressed();
					break;
			}
		}

	} // namespace gui
} // namespace spades
