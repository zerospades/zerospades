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
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "Client.h"
#include "IImage.h"
#include "IRenderer.h"
#include "NetClient.h"
#include "PaletteView.h"
#include "Player.h"
#include "World.h"
#include <Core/Settings.h>
#ifdef __ANDROID__
#include "../../android/native/TouchPalette.h"
#endif

DEFINE_SPADES_SETTING(cg_keyPaletteLeft, "Left");
DEFINE_SPADES_SETTING(cg_keyPaletteRight, "Right");
DEFINE_SPADES_SETTING(cg_keyPaletteUp, "Up");
DEFINE_SPADES_SETTING(cg_keyPaletteDown, "Down");
DEFINE_SPADES_SETTING(cg_keyExtendedPalette, "p");

DEFINE_SPADES_SETTING(cg_hudPalette, "1");
DEFINE_SPADES_SETTING(cg_hudPaletteSize, "128");
DEFINE_SPADES_SETTING(cg_extendedPalette, "0");

SPADES_SETTING(cg_stats);
SPADES_SETTING(cg_statsSmallFont);

namespace spades {
	namespace client {
		static IntVector3 SanitizeCol(IntVector3 col) {
			if (col.x < 0) col.x = 0;
			if (col.y < 0) col.y = 0;
			if (col.z < 0) col.z = 0;
			return col;
		}

		PaletteView::PaletteView(Client* c) : client(c), renderer(c->GetRenderer()) {
			extended = cg_extendedPalette;
			UpdatePaletteSize();
			ResetColors();
			time = 0.0F;
			defaultColor = 3;
		}

		void PaletteView::UpdatePaletteSize() {
			paletteSize = extended ? 16 : 8;
		}

		void PaletteView::ResetColors() {
			colors.clear();

			static const IntVector3 palette[8] = {
				{128, 128, 128}, {256, 0, 0},  {256, 128, 0},
				{256, 256, 0},   {0, 256, 0},  {0, 256, 256},
				{0, 0, 256},     {256, 0, 256}
			};

			static const IntVector3 extendedPalette[16] = {
			  {128, 128, 128}, {256, 0, 0},     {256, 128, 0},   {256, 256, 0},
			  {256, 256, 128}, {128, 256, 0},   {0, 256, 0},     {0, 256, 128},
			  {0, 256, 256},   {128, 256, 256}, {0, 128, 256},   {0, 0, 256},
			  {128, 0, 256},   {256, 0, 256},   {256, 128, 256}, {256, 0, 128}
			};

			const auto* cols = extended ? extendedPalette : palette;
			const auto def = MakeIntVector3(256, 256, 256);

			for (int i = 0; i < paletteSize; ++i) {
				const auto& col = cols[i];
				for (int j = 1; j < paletteSize; j += 2)
					colors.push_back(SanitizeCol((col * j) / paletteSize - 1));
				for (int j = 1; j < paletteSize; j += 2)
					colors.push_back(col + (((def - col) * j) / paletteSize - 1));
			}
		}

		PaletteView::~PaletteView() {}

		int PaletteView::GetSelectedIndex() {
			World* w = client->GetWorld();
			if (!w)
				return -1;
			stmp::optional<Player&> p = w->GetLocalPlayer();
			if (!p)
				return -1;

			IntVector3 col = p->GetBlockColor();
			for (int i = 0; i < (int)colors.size(); i++) {
				if (col == colors[i])
					return i;
			}
			return -1;
		}

		int PaletteView::GetSelectedOrDefaultIndex() {
			int c = GetSelectedIndex();
			if (c == -1)
				return defaultColor;
			else
				return c;
		}

		bool PaletteView::KeyInput(const std::string& name, bool down) {
			if (EqualsIgnoringCase(name, cg_keyPaletteLeft)) {
				paletteInput.left = down;
				return true;
			} else if (EqualsIgnoringCase(name, cg_keyPaletteRight)) {
				paletteInput.right = down;
				return true;
			} else if (EqualsIgnoringCase(name, cg_keyPaletteUp)) {
				paletteInput.up = down;
				return true;
			} else if (EqualsIgnoringCase(name, cg_keyPaletteDown)) {
				paletteInput.down = down;
				return true;
			} else if (EqualsIgnoringCase(name, cg_keyExtendedPalette) && down) {
				extended = !extended;
				UpdatePaletteSize();
				ResetColors();
				cg_extendedPalette = extended;
				return true;
			}
			return false;
		}

		void PaletteView::Update(float dt) {
			time += dt;

			bool mode = cg_extendedPalette;
			if (extended != mode) {
				extended = mode;
				UpdatePaletteSize();
				ResetColors();
			}

			if (time < 0.1F)
				return;

			if (paletteInput.left || paletteInput.right || paletteInput.up || paletteInput.down) {
				int c = GetSelectedOrDefaultIndex();
				int cols = static_cast<int>(colors.size());

				bool changed = false;

				// handle horizontal navigation
				if (paletteInput.left || paletteInput.right) {
					changed = true;

					if (paletteInput.left) {
						if (c == 0)
							c = cols - 1;
						else
							c--;
					} else if (paletteInput.right) {
						if (c == cols - 1)
							c = 0;
						else
							c++;
					}
				}

				// handle vertical navigation
				if (paletteInput.up || paletteInput.down) {
					changed = true;

					if (paletteInput.up) {
						if (c < paletteSize)
							c += cols - paletteSize;
						else
							c -= paletteSize;
					} else if (paletteInput.down) {
						if (c >= cols - paletteSize)
							c -= cols - paletteSize;
						else
							c += paletteSize;
					}
				}

				// apply changes if any
				if (changed) {
					client->SetBlockColor(colors[c]);
					time = 0.0F; // reset
				}
			}
		}

#ifdef __ANDROID__
		void PaletteView::SelectAndroidPalettePoint(float x, float y) {
			if (!client->AndroidHasLivePlayer() || client->NeedsAbsoluteMouseCoordinate()) return;
			auto player = client->GetWorld()->GetLocalPlayer();
			if (!player || !player->IsToolBlock()) return;
			int index = android::PaletteIndex(x, y, paletteSize);
			if (index < 0 || static_cast<size_t>(index) >= colors.size()) return;
			// A drag may report several events for a single cell; avoid network spam.
			if (!(player->GetBlockColor() == colors[index])) client->SetBlockColor(colors[index]);
		}
#endif

		void PaletteView::Draw() {
			float sw = renderer.ScreenWidth();
			float sh = renderer.ScreenHeight();

			float wndSize = cg_hudPaletteSize;
			if (!extended)
				wndSize *= 0.5F;

			float cellGap = 1.0F;
			float bgPadding = 2.0F;
			float cellSize = wndSize / paletteSize;
			float totalSize = wndSize + (paletteSize - 1) * cellGap;

			float winX = (sw - 8.0F) - totalSize - bgPadding;
			float winY = (sh - 8.0F) - totalSize - 42.0F;

			const int statsMode = cg_stats;
			if (statsMode == 1)
				winY -= cg_statsSmallFont ? 2.0F : 12.0F;

#ifdef __ANDROID__
			// Keep this geometry aligned with the touch hit region in HudLayout.
			bool androidHud = client->AndroidHasLivePlayer() && !client->NeedsAbsoluteMouseCoordinate();
			if (androidHud) {
				float scale = sh / 980.0F;
				cellGap = android::PaletteGap * scale;
				bgPadding = 2.0F * scale;
				totalSize = android::PaletteSize * scale;
				cellSize = (totalSize - (paletteSize - 1) * cellGap) / paletteSize;
				winX = android::PaletteX * scale;
				winY = android::PaletteY * scale;
			}
#endif

			float bgX = winX - bgPadding;
			float bgY = winY - bgPadding;
			float bgSize = totalSize + 2 * bgPadding;

			// draw background
			renderer.SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1) * 0.07F);
			renderer.DrawFilledRect(bgX, bgY, bgX + bgSize, bgY + bgSize);

			renderer.SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1) * 0.1F);
			renderer.DrawOutlinedRect(bgX - 1, bgY - 1, bgX + bgSize + 1, bgY + bgSize + 1);

			int sel = GetSelectedIndex();
			for (size_t phase = 0; phase < 2; phase++) {
				for (size_t i = 0; i < colors.size(); i++) {
					bool selected = sel == static_cast<int>(i);
					if (selected != (phase == 1))
						continue;

					int row = static_cast<int>(i / paletteSize);
					int col = static_cast<int>(i % paletteSize);

					float x = winX + col * (cellSize + cellGap);
					float y = winY + row * (cellSize + cellGap);
					float w = x + cellSize;
					float h = y + cellSize;

					renderer.SetColorAlphaPremultiplied(ConvertColorRGBA(colors[i]));
					renderer.DrawFilledRect(x + 1, y + 1, w - 1, h - 1);

					if (selected) {
						float p = 0.5F * (1.0F + sinf(client->GetTime() * kPi * 4.0F));
						renderer.SetColorAlphaPremultiplied(MakeVector4(p, p, p, 1));
					} else {
						renderer.SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1) * 0.2F);
					}
					renderer.DrawOutlinedRect(x, y, w, h);
				}
			}

			if (cg_hudPalette
#ifdef __ANDROID__
			    && !androidHud
#endif
			   )
				client->DrawBlockPaletteHUD(winY);
		}
	} // namespace client
} // namespace spades
