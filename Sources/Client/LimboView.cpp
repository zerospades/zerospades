/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "Client.h"
#include "Fonts.h"
#include "IAudioChunk.h"
#include "IAudioDevice.h"
#include "IFont.h"
#include "IImage.h"
#include "IRenderer.h"
#include "LimboView.h"
#include "World.h"
#include <Core/Strings.h>

namespace spades {
	namespace client {

		// TODO: make limbo view scriptable using the existing UI framework.

		LimboView::LimboView(Client* client) : client(client), renderer(client->GetRenderer()) {
			// layout now!
			float menuWidth = 200.0F;
			float menuHeight = menuWidth / 8.0F;
			float rowHeight = menuHeight + 3.0F;

			float sw = renderer.ScreenWidth();
			float sh = renderer.ScreenHeight();

			float contentsWidth = sw - 8.0F;
			float maxContentsWidth = 800.0F;
			if (contentsWidth > maxContentsWidth)
				contentsWidth = maxContentsWidth;

			float left = (sw - contentsWidth) * 0.5F;
			float top = sh - 150.0F;

			float teamX = left + 10.0F;
			float firstY = top + 35.0F;

			World* w = client->GetWorld();

			items.push_back(MenuItem(MenuTeam1,
				AABB2(teamX, firstY, menuWidth, menuHeight),
				w ? w->GetTeamName(0) : "Team 1"));
			items.push_back(MenuItem(MenuTeam2,
				AABB2(teamX, firstY + rowHeight, menuWidth, menuHeight),
				w ? w->GetTeamName(1) : "Team 2"));
			items.push_back(MenuItem(MenuTeamSpectator,
				AABB2(teamX, firstY + rowHeight * 2.0F, menuWidth, menuHeight),
				_Tr("Client", "Spectator")));

			float weapX = left + 260.0F;

			items.push_back(MenuItem(MenuWeaponRifle,
				AABB2(weapX, firstY, menuWidth, menuHeight),
				_Tr("Client", "Rifle")));
			items.push_back(MenuItem(MenuWeaponSMG,
				AABB2(weapX, firstY + rowHeight, menuWidth, menuHeight),
				_Tr("Client", "SMG")));
			items.push_back(MenuItem(MenuWeaponShotgun,
				AABB2(weapX, firstY + rowHeight * 2.0F, menuWidth, menuHeight),
				_Tr("Client", "Shotgun")));

			//! The "Spawn" button that you press when you're ready to "spawn".
			items.push_back(MenuItem(MenuSpawn,
				AABB2(left + contentsWidth - 166.0F, firstY + 4.0F, 156.0F, 64.0F),
				_Tr("Client", "Spawn")));

			items.push_back(MenuItem(MenuClose,
				AABB2(left + contentsWidth - 24.0F, top, 24.0F, 24.0F), "X"));

			cursorPos = MakeVector2(sw * 0.5F, sh * 0.5F);

			selectedTeam = 2;
			selectedWeapon = RIFLE_WEAPON;
		}
		LimboView::~LimboView() {}

		void LimboView::MouseEvent(float x, float y) {
			cursorPos.x = Clamp(x, 0.0F, renderer.ScreenWidth());
			cursorPos.y = Clamp(y, 0.0F, renderer.ScreenHeight());
		}

		void LimboView::KeyEvent(const std::string& key) {
			if (key == "LeftMouseButton") {
				for (const auto& item : items) {
					if (item.hover) {
						IAudioDevice& dev = *client->audioDevice;
						Handle<IAudioChunk> c = dev.RegisterSound("Sounds/Feedback/Limbo/Select.opus");
						dev.PlayLocal(c.GetPointerOrNull(), AudioParam());
						switch (item.type) {
							case MenuTeam1: selectedTeam = 0; break;
							case MenuTeam2: selectedTeam = 1; break;
							case MenuTeamSpectator: selectedTeam = 2; break;
							case MenuWeaponRifle: selectedWeapon = RIFLE_WEAPON; break;
							case MenuWeaponSMG: selectedWeapon = SMG_WEAPON; break;
							case MenuWeaponShotgun: selectedWeapon = SHOTGUN_WEAPON; break;
							case MenuSpawn: client->SpawnPressed(); break;
							case MenuClose: client->CloseLimboView(); break;
						}
					}
				}
			} else if (key == "1") {
				if (selectedTeam >= 2) {
					selectedTeam = 0;
				} else {
					selectedWeapon = RIFLE_WEAPON;
					client->SpawnPressed();
				}
			} else if (key == "2") {
				if (selectedTeam >= 2) {
					selectedTeam = 1;
				} else {
					selectedWeapon = SMG_WEAPON;
					client->SpawnPressed();
				}
			} else if (key == "3") {
				if (selectedTeam < 2)
					selectedWeapon = SHOTGUN_WEAPON;
				client->SpawnPressed(); // if we have 3 and are already spec someone wants to spec..
			} else if (key == "Enter") {
				client->SpawnPressed();
			}
		}

		void LimboView::Update(float dt) {
			// spectator team was actually 255
			if (selectedTeam > 2)
				selectedTeam = 2;

			for (size_t i = 0; i < items.size(); i++) {
				MenuItem& item = items[i];
				item.visible = true;

				switch (item.type) {
					case MenuWeaponRifle:
					case MenuWeaponShotgun:
					case MenuWeaponSMG:
						if (selectedTeam >= 2)
							item.visible = false;
						break;
					case MenuClose:
						item.visible = client->HasLocalPlayer();
						break;
					default:;
				}

				bool newHover = item.rect && cursorPos;
				if (!item.visible)
					newHover = false;
				if (newHover && !item.hover) {
					IAudioDevice& dev = *client->audioDevice;
					Handle<IAudioChunk> c = dev.RegisterSound("Sounds/Feedback/Limbo/Hover.opus");
					dev.PlayLocal(c.GetPointerOrNull(), AudioParam());
				}
				item.hover = newHover;
			}
		}

		void LimboView::Draw() {
			World* w = client->GetWorld();

			IFont& font = client->fontManager->GetGuiFont();

			float sw = renderer.ScreenWidth();
			float sh = renderer.ScreenHeight();

			float contentsWidth = sw - 8.0F;
			float maxContentsWidth = 800.0F;
			if (contentsWidth > maxContentsWidth)
				contentsWidth = maxContentsWidth;

			float left = (sw - contentsWidth) * 0.5F;
			float top = sh - 150.0F;

			float height = 140.0F;

			// draw background
			renderer.SetColorAlphaPremultiplied(MakeVector4(0.0F, 0.0F, 0.0F, 0.5F));
			renderer.DrawFilledRect(left, top, left + contentsWidth, top + height);

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
				Vector2 pos = {left + 260.0F, top + 10.0F};
				font.DrawShadow(str, pos, 1.0F, color, shadowColor);
			}

			for (const auto& item : items) {
				if (!item.visible)
					continue;

				bool selected = false;
				int index = 0;
				switch (item.type) {
					case MenuTeam1:
					case MenuTeam2:
					case MenuTeamSpectator:
						selected = (selectedTeam == item.type);
						index = (selectedTeam >= 2) ? (1 + item.type) : 0;
						break;
					case MenuWeaponRifle:
					case MenuWeaponSMG:
					case MenuWeaponShotgun:
						selected = (selectedWeapon == (item.type - 3));
						index = (selectedTeam < 2) ? (1 + (item.type - 3)) : 0;
						break;
					default: selected = false;
				}

				Vector4 fillColor = MakeVector4(0.2F, 0.2F, 0.2F, 0.5F);
				if (selected)
					fillColor = MakeVector4(0.7F, 0.7F, 0.7F, 1) * 0.9F;
				else if (item.hover)
					fillColor = MakeVector4(0.4F, 0.4F, 0.4F, 1) * 0.7F;

				renderer.SetColorAlphaPremultiplied(fillColor);
				renderer.DrawImage(nullptr, item.rect);

				renderer.SetColorAlphaPremultiplied(fillColor * 0.8F);
				renderer.DrawOutlinedRect(item.rect.GetMinX(), item.rect.GetMinY(),
										  item.rect.GetMaxX(), item.rect.GetMaxY());

				if (item.type == MenuSpawn || item.type == MenuClose) {
					Vector2 size = font.Measure(item.text);
					Vector2 pos = item.rect.min;
					pos.x += (item.rect.GetWidth() - size.x) * 0.5F;
					pos.y += (item.rect.GetHeight() - size.y) * 0.5F;

					if (item.type == MenuClose) {
						font.DrawShadow(item.text, pos, 1.0F, color, shadowColor);

						// draw hotkey hint
						std::string hotKeyText = _Tr("Client", "[Esc]");
						Vector2 hotKeySize = font.Measure(hotKeyText);
						Vector2 hotKeyPos = {item.rect.GetMinX() - hotKeySize.x - 5.0F, pos.y};
						font.DrawShadow(hotKeyText, hotKeyPos, 1.0F, hotkeyColor, shadowColor);
					} else if (item.type == MenuSpawn) {
						Vector2 padPos = item.rect.min + MakeVector2(8.0F, 8.0F);
						Vector2 padSize = MakeVector2(item.rect.GetWidth() - 16.0F, item.rect.GetHeight() - 16.0F);
						Vector2 txtSize = font.Measure(item.text);
						Vector2 txtPos = {padPos.x, padPos.y + (padSize.y - txtSize.y) * 0.5F};
						font.DrawShadow(item.text, txtPos, 1.0F, color, shadowColor);

						// draw hotkey hint
						std::string hotKeyText = _Tr("Client", "[1, 2, 3]");
						Vector2 hotKeySize = font.Measure(hotKeyText);
						Vector2 hotKeyPos = {padPos.x + padSize.x - hotKeySize.x, padPos.y + (padSize.y - hotKeySize.y) * 0.5F};
						font.DrawShadow(hotKeyText, hotKeyPos, 1.0F, hotkeyColor, shadowColor);
					}
				} else {
					std::string str = item.text;
					if (item.type == MenuTeam1)
						str = w->GetTeamName(0);
					else if (item.type == MenuTeam2)
						str = w->GetTeamName(1);

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
			Handle<IImage> cursor = renderer.RegisterImage("Gfx/UI/Cursor.png");
			renderer.SetColorAlphaPremultiplied(color);
			renderer.DrawImage(cursor, AABB2(cursorPos.x - 8, cursorPos.y - 8, 32, 32));
		}
	} // namespace client
} // namespace spades