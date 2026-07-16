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

#include <cctype>

#include "DemoListView.h"
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/MainScreenHelper.h>
#include <Gui/UI/Framework/TextUtils.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {
		using ui::FormatFileSize;
		using ui::SetColorNP;
		using ui::UIElement;
		using ui::UIManager;

		std::string StripDemoPath(std::string path) {
			// Remove everything up to and including the last '/' (handles both
			// relative "Demos/name.dem" and absolute "/some/dir/Demos/name.dem").
			size_t slash = path.rfind('/');
			if (slash != std::string::npos)
				path = path.substr(slash + 1);
			// Remove ".dem" suffix
			if (path.size() > 4 && path.substr(path.size() - 4) == ".dem")
				path = path.substr(0, path.size() - 4);
			return path;
		}

		namespace {
			bool DemoIsDigit(char c) { return c >= '0' && c <= '9'; }

			// Replace underscores with spaces for display (unescape '-'-encoded fields).
			std::string DemoUnderscoreToSpace(std::string s) {
				for (char& c : s) {
					if (c == '_')
						c = ' ';
				}
				return s;
			}

			// Capitalize the first letter of a string.
			std::string DemoCapFirst(std::string s) {
				if (!s.empty() && s[0] >= 'a' && s[0] <= 'z')
					s[0] = static_cast<char>(s[0] - 'a' + 'A');
				return s;
			}

			// Return a human-readable game mode label from a sanitized mode token.
			std::string DemoFormatGameMode(const std::string& raw) {
				if (raw == "ctf")
					return "CTF";
				if (raw == "tc")
					return "TC";
				if (raw == "arena")
					return "Arena";
				if (raw == "game")
					return "Game";
				return DemoCapFirst(raw);
			}

			// Split a string on '-' and return the parts.
			std::vector<std::string> DemoSplitByDash(const std::string& s) {
				std::vector<std::string> result;
				size_t start = 0;
				for (size_t i = 0; i <= s.size(); i++) {
					if (i == s.size() || s[i] == '-') {
						result.push_back(s.substr(start, i - start));
						start = i + 1;
					}
				}
				return result;
			}
		} // namespace

		DemoInfo ParseDemoFilename(const std::string& path, MainScreenHelper& helper) {
			DemoInfo info;
			info.displayName = StripDemoPath(path);
			const std::string& name = info.displayName;

			std::int64_t sz = helper.GetDemoFileSize(path);
			info.fileSize = FormatFileSize(sz);

			// The timestamp prefix occupies exactly 16 characters:
			//	 YYYY-MM-DD-HH-MM  (positions 0-15)
			// followed by either end-of-string or a '-' separator.
			if (name.size() >= 16) {
				bool valid = DemoIsDigit(name[0]) && DemoIsDigit(name[1]) && DemoIsDigit(name[2]) &&
				             DemoIsDigit(name[3]) && name[4] == '-' && DemoIsDigit(name[5]) &&
				             DemoIsDigit(name[6]) && name[7] == '-' && DemoIsDigit(name[8]) &&
				             DemoIsDigit(name[9]) && name[10] == '-' && DemoIsDigit(name[11]) &&
				             DemoIsDigit(name[12]) && name[13] == '-' && DemoIsDigit(name[14]) &&
				             DemoIsDigit(name[15]);

				if (valid && (name.size() == 16 || name[16] == '-')) {
					// Format as "YYYY-MM-DD HH:MM"
					info.timestamp = name.substr(0, 4) + "-" + name.substr(5, 2) + "-" +
					                 name.substr(8, 2) + " " + name.substr(11, 2) + ":" +
					                 name.substr(14, 2);

					// Parse optional context: server[-map[-gamemode]]
					if (name.size() > 17) {
						std::string context = name.substr(17);
						std::vector<std::string> parts = DemoSplitByDash(context);

						// server: all parts except the last two
						// map:	   second-to-last part
						// mode:   last part
						// Minimum: just gamemode (1 part), server+gamemode (2 parts),
						// server+map+gamemode (3+ parts).
						if (parts.size() == 1) {
							info.gameMode = DemoFormatGameMode(parts[0]);
						} else if (parts.size() == 2) {
							info.serverName = DemoUnderscoreToSpace(parts[0]);
							info.gameMode = DemoFormatGameMode(parts[1]);
						} else {
							// server = parts[0..n-3] joined with space, map = parts[n-2],
							// mode = parts[n-1]
							std::string server = parts[0];
							for (size_t i = 1; i < parts.size() - 2; i++)
								server += " " + parts[i];
							info.serverName = DemoUnderscoreToSpace(server);
							info.mapName = DemoUnderscoreToSpace(parts[parts.size() - 2]);
							info.gameMode = DemoFormatGameMode(parts[parts.size() - 1]);
						}
					}

					info.structured = true;
				}
			}

			return info;
		}

		// -- DemoListItem --

		DemoListItem::DemoListItem(UIManager* manager, const std::string& filename, DemoInfo info,
		                           float colDateWidth, float colModeWidth, float colMapWidth,
		                           float colSizeWidth, float totalWidth)
		    : ui::ButtonBase(manager),
		      info(std::move(info)),
		      colDateWidth(colDateWidth),
		      colModeWidth(colModeWidth),
		      colMapWidth(colMapWidth),
		      colSizeWidth(colSizeWidth),
		      totalWidth(totalWidth),
		      filename(filename) {}

		void DemoListItem::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			client::IFont* font = GetFont();
			if (!font)
				return;
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			Vector4 bgcolor = MakeVector4(1.0F, 1.0F, 1.0F, 0.0F);
			Vector4 fgcolor = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);

			if (pressed && hover) {
				bgcolor.w = 0.3F;
			} else if (hover) {
				bgcolor.w = 0.15F;
			}

			SetColorNP(r, bgcolor);
			r.DrawImage(nullptr, AABB2(pos.x + 1.0F, pos.y + 1.0F, sz.x, sz.y));

			if (info.structured) {
				// Column order (left to right): Server | Map | Mode | Timestamp | Size
				// Server is variable-width; the four rightmost columns are fixed-width.
				// Use totalWidth (= contentsWidth from the layout) so that fixed-column
				// positions match the headers, which are also sized from contentsWidth.
				// (size.x is narrower by the ListView scrollbar width.)
				float fixedRight = colMapWidth + colModeWidth + colDateWidth + colSizeWidth;
				float serverWidth = totalWidth - fixedRight;
				float x = pos.x + 2.0F;
				font->Draw(info.serverName, MakeVector2(x, pos.y + 2.0F), 1.0F, fgcolor);
				x = pos.x + serverWidth + 2.0F;
				font->Draw(info.mapName, MakeVector2(x, pos.y + 2.0F), 1.0F, fgcolor);
				x += colMapWidth;
				font->Draw(info.gameMode, MakeVector2(x, pos.y + 2.0F), 1.0F, fgcolor);
				x += colModeWidth;
				font->Draw(info.timestamp, MakeVector2(x, pos.y + 2.0F), 1.0F, fgcolor);
				x += colDateWidth;
				font->Draw(info.fileSize, MakeVector2(x, pos.y + 2.0F), 1.0F, fgcolor);
			} else {
				// Filename does not match the structured format; display it as-is.
				font->Draw(info.displayName, pos + MakeVector2(2.0F, 2.0F), 1.0F, fgcolor);
				if (info.fileSize.size() > 0) {
					float sx = pos.x + totalWidth - colSizeWidth + 2.0F;
					font->Draw(info.fileSize, MakeVector2(sx, pos.y + 2.0F), 1.0F, fgcolor);
				}
			}
		}

		// -- DemoListModel --

		DemoListModel::DemoListModel(UIManager* manager, MainScreenHelper* helper,
		                             std::vector<std::string> list, float colDateWidth,
		                             float colModeWidth, float colMapWidth, float colSizeWidth,
		                             float totalWidth)
		    : manager(manager),
		      helper(helper),
		      list(std::move(list)),
		      colDateWidth(colDateWidth),
		      colModeWidth(colModeWidth),
		      colMapWidth(colMapWidth),
		      colSizeWidth(colSizeWidth),
		      totalWidth(totalWidth) {
			itemElements.resize(this->list.size());
		}

		void DemoListModel::OnItemClicked(UIElement& sender) {
			DemoListItem* item = dynamic_cast<DemoListItem*>(&sender);
			if (item && itemActivated)
				itemActivated(item->filename);
		}

		void DemoListModel::OnItemDoubleClicked(UIElement& sender) {
			DemoListItem* item = dynamic_cast<DemoListItem*>(&sender);
			if (item && itemDoubleClicked)
				itemDoubleClicked(item->filename);
		}

		Handle<UIElement> DemoListModel::CreateElement(int row) {
			if (!itemElements[row]) {
				DemoInfo info = ParseDemoFilename(list[row], *helper);
				Handle<DemoListItem> i =
				    Handle<DemoListItem>::New(manager, list[row], std::move(info), colDateWidth,
				                              colModeWidth, colMapWidth, colSizeWidth, totalWidth);
				i->activated = [this](UIElement& s) { OnItemClicked(s); };
				i->doubleClicked = [this](UIElement& s) { OnItemDoubleClicked(s); };
				itemElements[row] = i;
			}
			return itemElements[row].Cast<UIElement>();
		}

		// -- DemoListHeader --

		void DemoListHeader::Render() {
			client::IFont* font = GetFont();
			if (!font)
				return;
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			font->Draw(text, pos + MakeVector2(2.0F, (sz.y - font->Measure(text).y) * 0.5F), 1.0F,
			           MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
		}
	} // namespace gui
} // namespace spades
