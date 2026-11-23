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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "CTFGameMode.h"
#include "Client.h"
#include "Fonts.h"
#include "IFont.h"
#include "IImage.h"
#include "IRenderer.h"
#include "MapView.h"
#include "NetClient.h"
#include "Player.h"
#include "ScoreboardView.h"
#include "TCGameMode.h"
#include "World.h"
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Core/Strings.h>

SPADES_SETTING(cg_minimapPlayerColor);
SPADES_SETTING(cg_hudPlayerCount);

namespace spades {
	namespace client {

		static const Vector4 white = {1, 1, 1, 1};
		static const Vector4 gray = {0.5, 0.5, 0.5, 1};
		static const Vector4 spectatorIdColor = {210.0F / 255, 210.0F / 255, 210.0F / 255, 1}; // Grey
		static const Vector4 spectatorTextColor = {220.0F / 255, 220.0F / 255, 0, 1}; // Goldish yellow
		static const int spectatorTeamId = 255; // Spectators have a team id of 255

		static const float spectatorGap = 4.0F;
		static const float spectatorRowHeight = 20.0F;

		struct SpectatorEntry {
			int id;
			std::string idText;
			std::string name;
			float idWidth;
			float width; // id + spacing + name
		};

		// Gathers all spectators and measures how wide each one is
		static std::vector<SpectatorEntry> CollectSpectators(World& world, IFont& font) {
			std::vector<SpectatorEntry> result;
			for (size_t i = 0; i < world.GetNumPlayerSlots(); i++) {
				auto maybePlayer = world.GetPlayer(static_cast<unsigned int>(i));
				if (!maybePlayer)
					continue;
				Player& player = maybePlayer.value();
				if (player.GetTeamId() != spectatorTeamId)
					continue;

				SpectatorEntry ent;
				ent.id = static_cast<int>(i);
				ent.idText = "#" + ToString(ent.id);
				ent.name = player.GetName();
				ent.idWidth = font.Measure(ent.idText).x;
				ent.width = ent.idWidth + 2.0F + font.Measure(ent.name).x;
				result.push_back(ent);
			}
			return result;
		}

		// Greedy line wrapping. Returns the index of the first entry of each row.
		static std::vector<size_t> BuildSpectatorRowStarts(const std::vector<SpectatorEntry>& entries,
		                                                   float maxWidth) {
			std::vector<size_t> starts;
			float current = 0.0F;
			for (size_t i = 0; i < entries.size(); i++) {
				float w = entries[i].width;
				if (starts.empty()) {
					starts.push_back(i);
					current = w;
				} else if (current + spectatorGap + w > maxWidth) {
					starts.push_back(i);
					current = w;
				} else {
					current += spectatorGap + w;
				}
			}
			return starts;
		}

		ScoreboardView::ScoreboardView(Client* c) : client(c), renderer(c->GetRenderer()) {
			SPADES_MARK_FUNCTION();
			world = nullptr;
			tc = nullptr;
			ctf = nullptr;

			// Use GUI font if spectator string has special chars
			auto spectatorString = _TrN("Client", "Spectator{1}", "Spectators{1}", "", "");
			auto hasSpecialChar =
			  std::find_if(spectatorString.begin(), spectatorString.end(), [](char ch) {
				  return !(isalnum(static_cast<unsigned char>(ch)) || ch == '_');
			  }) != spectatorString.end();

			spectatorFont = hasSpecialChar
				? c->fontManager->GetMediumFont()
				: c->fontManager->GetSquareDesignFont();

			intelIcon = renderer.RegisterImage("Gfx/Map/Intel.png");
		}

		ScoreboardView::~ScoreboardView() {}

		int ScoreboardView::GetTeamScore(int team) const {
			if (ctf) {
				return ctf->GetTeam(team).score;
			} else if (tc) {
				int num = 0;
				for (int i = 0; i < tc->GetNumTerritories(); i++) {
					if (tc->GetTerritory(i).ownerTeamId == team)
						num++;
				}
				return num;
			} else {
				return 0;
			}
		}

		int ScoreboardView::GetCaptureLimit() const {
			if (ctf) {
				return ctf->GetCaptureLimit();
			} else if (tc) {
				return tc->GetNumTerritories();
			} else {
				return -1;
			}
		}

		Vector4 ScoreboardView::GetTeamColor(int team) {
			return ConvertColorRGBA(world->GetTeamColor(team));
		}

		void ScoreboardView::Draw() {
			SPADES_MARK_FUNCTION();

			world = client->GetWorld();
			if (!world)
				return; // no world

			// TODO: `ctf` and `tc` are only valid throughout the
			// method call's duration. Move them to a new context type
			stmp::optional<IGameMode&> mode = world->GetMode();
			ctf = (mode->ModeType() == IGameMode::m_CTF)
				? dynamic_cast<CTFGameMode*>(mode.get_pointer())
				: NULL;
			tc = (mode->ModeType() == IGameMode::m_TC)
				? dynamic_cast<TCGameMode*>(mode.get_pointer())
				: NULL;

			Handle<IImage> img;
			IFont& font = client->fontManager->GetSquareDesignFont();
			Vector2 pos, size;
			std::string str;

			float sw = renderer.ScreenWidth();
			float sh = renderer.ScreenHeight();

			Vector2 scrCenter = MakeVector2(sw, sh) * 0.5F;

			float contentsWidth = sw + 8.0F;
			float maxContentsWidth = 800.0F;
			if (contentsWidth > maxContentsWidth)
				contentsWidth = maxContentsWidth;

			float contentsHeight = sh - 156.0F;
			float maxContentsHeight = 360.0F;
			if (contentsHeight > maxContentsHeight)
				contentsHeight = maxContentsHeight;

			float teamBarTop = (sh - contentsHeight) * 0.5F;
			float teamBarHeight = 60.0F;
			float contentsLeft = (sw - contentsWidth) * 0.5F;
			float contentsRight = contentsLeft + contentsWidth;
			float contentsHalf = (contentsRight - contentsLeft) * 0.5F;

			int numPlayers = 0;
			int numSpectators = 0;
			int numAliveTeam1 = 0;
			int numAliveTeam2 = 0;
			for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
				auto maybePlayer = world->GetPlayer(static_cast<unsigned int>(i));
				if (!maybePlayer)
					continue;
				Player& player = maybePlayer.value();

				// count total players
				numPlayers++;

				// count spectators
				int teamId = player.GetTeamId();
				if (teamId == spectatorTeamId) {
					numSpectators++; 
					continue;
				}

				// count alive players
				if (!player.IsAlive())
					continue;
				if (teamId == 0)
					numAliveTeam1++;
				else if (teamId == 1)
					numAliveTeam2++;
			}

			bool manyPlayers = numPlayers > 32;
			float playersHeight = (manyPlayers ? 335.0F : 315.0F) - teamBarHeight;
			float playersTop = teamBarTop + teamBarHeight;
			float playersBottom = playersTop + playersHeight;

			bool areSpectatorsPresent = numSpectators > 0;
			float spectatorMaxWidth = contentsWidth - 40.0F;
			float spectatorsHeight = 0.0F;
			if (areSpectatorsPresent) {
				IFont& specFont = client->fontManager->GetGuiFont();
				auto specEntries = CollectSpectators(*world, specFont);
				size_t specRows = BuildSpectatorRowStarts(specEntries, spectatorMaxWidth).size();
				spectatorsHeight = 64.0F;
				if (specRows > 1)
					spectatorsHeight += (specRows - 1) * spectatorRowHeight;
			}
			float spectatorsBottom = playersBottom + spectatorsHeight;

			// draw shadow
			img = renderer.RegisterImage("Gfx/Scoreboard/TopShadow.png");
			size.y = 32.0F;
			renderer.SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.2F));
			renderer.DrawImage(img, AABB2(0, teamBarTop - size.y, sw, size.y));
			renderer.DrawImage(img, AABB2(0, spectatorsBottom + size.y, sw, -size.y));

			// draw team bar
			renderer.SetColorAlphaPremultiplied(AdjustColor(GetTeamColor(0), 0.8F, 0.3F));
			renderer.DrawImage(nullptr, AABB2(0, teamBarTop, scrCenter.x, teamBarHeight));
			renderer.SetColorAlphaPremultiplied(AdjustColor(GetTeamColor(1), 0.8F, 0.3F));
			renderer.DrawImage(nullptr, AABB2(scrCenter.x, teamBarTop, scrCenter.x, teamBarHeight));

			img = renderer.RegisterImage("Gfx/Scoreboard/Grunt.png");
			size.x = 120.0F;
			size.y = 60.0F;
			renderer.DrawImage(img, AABB2(contentsLeft, playersTop - size.y, size.x, size.y));
			renderer.DrawImage(img, AABB2(contentsRight, playersTop - size.y, -size.x, size.y));

			// draw team name
			str = world->GetTeamName(0);
			pos.x = contentsLeft + 120.0F;
			pos.y = teamBarTop + 5.0F;
			font.Draw(str, pos + MakeVector2(1, 2), 1.0F, MakeVector4(0, 0, 0, 0.5));
			font.Draw(str, pos, 1.0F, white);

			str = world->GetTeamName(1);
			pos.x = contentsRight - font.Measure(str).x - 120.0F;
			pos.y = teamBarTop + 5.0F;
			font.Draw(str, pos + MakeVector2(1, 2), 1.0F, MakeVector4(0, 0, 0, 0.5));
			font.Draw(str, pos, 1.0F, white);

			// draw alive player count
			if ((int)cg_hudPlayerCount >= 3) {
				img = renderer.RegisterImage("Gfx/IconUser.png");
				IFont& guiFont = client->fontManager->GetGuiFont();

				float iconSize = 12.0F;
				float counterTop = playersTop - 10.0F;

				// team 1
				str = ToString(numAliveTeam1);
				size = guiFont.Measure(str);
				pos.x = scrCenter.x - 5.0F - iconSize;
				pos.y = counterTop - 2.0F - (size.y - iconSize) * 0.5F;
				renderer.SetColorAlphaPremultiplied(white * 0.5F);
				renderer.DrawImage(img, pos);

				pos.x -= size.x + 2.0F;
				pos.y = counterTop - size.y * 0.5F;
				guiFont.Draw(str, pos, 1.0F, MakeVector4(1, 1, 1, 0.5));

				// team 2
				str = ToString(numAliveTeam2);
				pos.x = scrCenter.x + 5.0F;
				pos.y = counterTop - 2.0F - (size.y - iconSize) * 0.5F;
				renderer.SetColorAlphaPremultiplied(white * 0.5F);
				renderer.DrawImage(img, pos);

				pos.x += iconSize + 2.0F;
				pos.y = counterTop - size.y * 0.5F;
				guiFont.Draw(str, pos, 1.0F, MakeVector4(1, 1, 1, 0.5));
			}

			// draw scores
			int capLimit = GetCaptureLimit();
			if (capLimit != -1) {
				str = Format("{0}-{1}", GetTeamScore(0), capLimit);
				pos.x = scrCenter.x - font.Measure(str).x - 15.0F;
				pos.y = teamBarTop + 5.0F;
				font.Draw(str, pos, 1.0F, MakeVector4(1, 1, 1, 0.5));

				str = Format("{0}-{1}", GetTeamScore(1), capLimit);
				pos.x = scrCenter.x + 15.0F;
				font.Draw(str, pos, 1.0F, MakeVector4(1, 1, 1, 0.5));
			}

			// players background
			img = renderer.RegisterImage("Gfx/Scoreboard/PlayersBg.png");
			renderer.SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 1));
			renderer.DrawImage(img, AABB2(0, playersTop, sw, playersHeight + spectatorsHeight));

			// draw players
			DrawPlayers(0, contentsLeft, playersTop, contentsHalf, playersHeight);
			DrawPlayers(1, (contentsLeft + contentsHalf) - 4.0F, playersTop, contentsHalf, playersHeight);
			if (areSpectatorsPresent)
				DrawSpectators(scrCenter.x, playersBottom, spectatorMaxWidth);
		}

		struct ScoreboardEntry {
			int id;
			int score;
			std::string name;
			bool alive;
			bool operator<(const ScoreboardEntry& ent) const { return score > ent.score; }
		};

		extern int palette[32][3];

		void ScoreboardView::DrawPlayers(int team, float left, float top, float width, float height) {
			char buf[16];
			Vector2 size;
			
			int numPlayers = 0;
			std::vector<ScoreboardEntry> entries;
			for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
				auto maybePlayer = world->GetPlayer(static_cast<unsigned int>(i));
				if (!maybePlayer)
					continue;
				Player& player = maybePlayer.value();
				if (player.GetTeamId() != team)
					continue;

				ScoreboardEntry ent;
				ent.name = player.GetName();
				ent.score = player.GetScore();
				ent.alive = player.IsAlive();
				ent.id = static_cast<int>(i);
				entries.push_back(ent);
				numPlayers++;
			}

			if (numPlayers == 0)
				return;

			std::sort(entries.begin(), entries.end());

			bool manyPlayers = numPlayers > 32;
			IFont& font = manyPlayers
				? client->fontManager->GetSmallFont()
				: client->fontManager->GetGuiFont();
			float rowHeight = manyPlayers ? 12.0F : 24.0F;

			int maxRows = (int)floorf(height / rowHeight);
			int cols = std::max(1, (numPlayers + maxRows - 1) / maxRows);
			maxRows = (numPlayers + cols - 1) / cols;

			int row = 0, col = 0;
			float colWidth = width / (float)cols;

			const int colorMode = cg_minimapPlayerColor;

			for (const auto& ent : entries) {
				float rowY = top + 6.0F + row * rowHeight;
				float colX = left + col * colWidth;

				// draw player id
				snprintf(buf, sizeof(buf), "#%d", ent.id); // FIXME: 1-base?
				size = font.Measure(buf);
				if (colorMode) {
					int colorIndex = ent.id % 32;
					IntVector3 colorplayer = MakeIntVector3(
						palette[colorIndex][0],
						palette[colorIndex][1],
						palette[colorIndex][2]
					);
					Vector4 colorplayerF = ModifyColor(colorplayer);
					font.Draw(buf, MakeVector2(colX + 35.0F - size.x, rowY), 1.0F, colorplayerF);
				} else {
					font.Draw(buf, MakeVector2(colX + 35.0F - size.x, rowY), 1.0F, white);
				}

				// draw player name
				Vector4 color = ent.alive ? white : gray;
				if (stmp::make_optional(ent.id) == world->GetLocalPlayerIndex()) {
					color = GetTeamColor(team);

					// make it brighter for darker colors
					float luminosity = color.x + color.y + color.z;
					if (luminosity <= 0.1F)
						color = MakeVector4(0, 1, 1, 1);
					else if (luminosity <= 0.9F)
						color += (white - color) * 0.5F;
				}
				font.Draw(ent.name, MakeVector2(colX + 40.0F, rowY), 1.0F, color);

				// draw player score
				snprintf(buf, sizeof(buf), "%d", ent.score);
				size = font.Measure(buf);
				font.Draw(buf, MakeVector2(colX + colWidth - 5.0F - size.x, rowY), 1.0F, white);

				// draw intel icon
				if (ctf) {
					const auto& t = ctf->GetTeam(team);
					if (t.hasIntel && ent.id == static_cast<int>(t.carrierId)) {
						Vector2 pos = MakeVector2(colX + colWidth - 25.0F - size.x, rowY + 1.0F);
						float pulse = std::max(0.5F, fabsf(sinf(world->GetTime() * 4.0F)));
						renderer.SetColorAlphaPremultiplied(white * pulse);
						renderer.DrawImage(intelIcon, pos.Floor());
					}
				}

				row++;
				if (row >= maxRows) {
					col++;
					row = 0;
				}
			}
		}

		void ScoreboardView::DrawSpectators(float centerX, float top, float maxWidth) const {
			IFont& font = client->fontManager->GetGuiFont();

			auto entries = CollectSpectators(*world, font);
			if (entries.empty())
				return;

			std::string title = _TrN("Client", "Spectator{1}", "Spectators{1}", (int)entries.size(), "");

			auto sizeTitle = spectatorFont->Measure(title);
			Vector2 pos = MakeVector2(centerX, top + 10.0F) - sizeTitle * 0.5F;
			spectatorFont->Draw(title, pos + MakeVector2(1, 2), 1.0F, MakeVector4(0, 0, 0, 0.5));
			spectatorFont->Draw(title, pos, 1.0F, spectatorTextColor);

			auto rowStarts = BuildSpectatorRowStarts(entries, maxWidth);
			float yOffset = pos.y + sizeTitle.y + 10.0F;

			for (size_t r = 0; r < rowStarts.size(); r++) {
				size_t begin = rowStarts[r];
				size_t end = (r + 1 < rowStarts.size()) ? rowStarts[r + 1] : entries.size();

				// Measure this row so it can be centered on its own
				float rowWidth = 0.0F;
				for (size_t i = begin; i < end; i++)
					rowWidth += entries[i].width + (i > begin ? spectatorGap : 0.0F);

				float x = centerX - rowWidth * 0.5F;
				for (size_t i = begin; i < end; i++) {
					const auto& ent = entries[i];
					font.Draw(ent.idText, MakeVector2(x, yOffset), 1.0F, spectatorIdColor);
					font.Draw(ent.name, MakeVector2(x + ent.idWidth + 2.0F, yOffset), 1.0F, white);
					x += ent.width + spectatorGap;
				}
				yOffset += spectatorRowHeight;
			}
		}
	} // namespace client
} // namespace spades