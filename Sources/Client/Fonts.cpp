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

#include <memory>
#include <regex>

#include <utility>
#include <vector>

#include "FTFont.h"
#include "FontData.h"
#include "Fonts.h"
#include "IRenderer.h"
#include "Quake3Font.h"
#include <Core/Bitmap.h>
#include <Core/CP437.h>
#include <Core/FileManager.h>

namespace spades {
	namespace client {
		namespace {
			std::regex const g_fontNameRe(".*\\.(?:otf|ttf|ttc)", std::regex::icase);

			struct GlobalFontInfo {
				std::shared_ptr<ngclient::FTFontSet> squareFontSet, guiFontSet;

				GlobalFontInfo() {
					SPLog("Loading built-in fonts");
					LoadFontSet(squareFontSet, "Gfx/Fonts/SquareFont.ttf", "SquareFont");
					LoadFontSet(guiFontSet, "Gfx/Fonts/OpenSans.ttf", "Open Sans");

					// Preliminary custom font support
					auto files = FileManager::EnumFiles("Fonts");
					for (const auto& name : files) {
						if (!std::regex_match(name, g_fontNameRe))
							continue;

						SPLog("Loading custom font '%s'", name.c_str());

						auto path = "Fonts/" + name;
						guiFontSet->AddFace(path);
					}
				}

				void LoadFontSet(std::shared_ptr<ngclient::FTFontSet>& fontSet,
					const std::string& path, const std::string& name) {
					fontSet = std::make_shared<ngclient::FTFontSet>();
					if (FileManager::FileExists(path.c_str())) {
						fontSet->AddFace(path);
						SPLog("Font '%s' loaded", name.c_str());
					} else {
						SPLog("Font '%s' was not found", name.c_str());
					}
				}

				static GlobalFontInfo& GetInstance() {
					static GlobalFontInfo instance;
					return instance;
				}
			};

			// The bitmap fonts are 16x16 grids of cells ordered by code page 437. Their pixel
			// data is embedded in FontData.h, so there is no image file to load.
			Handle<IFont> CreateBitmapFont(IRenderer* renderer, Handle<Bitmap> source, fontdata::CellSize cell) {
				// Quake3Font reads 4 ints per glyph (x, y, width, advance), indexed by Unicode
				// code point. A width of -1 marks the glyph as missing.
				std::vector<int> map(256 * 4, 0);
				for (int i = 0; i < 256; i++)
					map[i * 4 + 2] = -1;

				auto setGlyph = [&](int codePoint, int cellIndex) {
					int* e = &map[codePoint * 4];
					e[0] = (cellIndex % 16) * cell.width;
					e[1] = (cellIndex / 16) * cell.height;
					e[2] = cell.width;
					e[3] = cell.width;
				};

				for (int c = 33; c <= 126; c++)
					setGlyph(c, c);

				// The space is handled as a gap of `spaceWidth` instead of an image.
				setGlyph(32, 32);
				map[32 * 4 + 3] = PROP_SPACE_WIDTH;

				// Latin-1 code points that have a glyph in code page 437. The bitmap font is
				// indexed by Unicode code point (< 256), so anything above that is unreachable.
				for (int cell = 128; cell < 256; cell++) {
					const uint32_t codePoint = CP437::DecodeChar(static_cast<char>(cell));
					if (codePoint >= 128 && codePoint < 256)
						setGlyph(static_cast<int>(codePoint), cell);
				}

				Handle<IImage> img = renderer->CreateImage(*source);

				// The font keeps `source`: its pixel data is needed to generate the outline atlas.
				return Handle<Quake3Font>::New(renderer, img.GetPointerOrNull(),
					source.GetPointerOrNull(), map.data(),
					cell.height, static_cast<float>(cell.width), true).Cast<IFont>();
			}
		} // namespace

		FontManager::FontManager(IRenderer* renderer) {
			auto& instance = GlobalFontInfo::GetInstance();

			squareDesignFont = Handle<ngclient::FTFont>::New(renderer, instance.squareFontSet, 36.0F, 36.0F).Cast<IFont>();
			largeFont = Handle<ngclient::FTFont>::New(renderer, instance.guiFontSet, 34.0F, 34.0F).Cast<IFont>();
			mediumFont = Handle<ngclient::FTFont>::New(renderer, instance.guiFontSet, 24.0F, 24.0F).Cast<IFont>();
			headingFont = Handle<ngclient::FTFont>::New(renderer, instance.guiFontSet, 20.0F, 20.0F).Cast<IFont>();
			guiFont = Handle<ngclient::FTFont>::New(renderer, instance.guiFontSet, 16.0F, 20.0F).Cast<IFont>();
			// 12px gui face with line height == em height: Measure() then reports the true
			// glyph extent instead of the padded line box, so vertical centering is tight.
			smallGuiFont = Handle<ngclient::FTFont>::New(renderer, instance.guiFontSet, 12.0F, 12.0F).Cast<IFont>();

			// bitmap fonts
			// FIXME: licensing. The 6x8 glyphs (fontdata::kFont6x8) come from VOXLAP5.C by
			// Ken Silverman, the engine of Ben Aksoy's Ace of Spades, under the Voxlap
			// license, not the GPL. Replace them with a font under a GPL-compatible license
			// (or one drawn for the project) that keeps the look.
			smallFont = CreateBitmapFont(renderer, fontdata::Build6x8Atlas(), fontdata::k6x8Cell);
		}

		FontManager::~FontManager() {}
	} // namespace client
} // namespace spades