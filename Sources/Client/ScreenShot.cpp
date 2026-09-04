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

#include <cstdio>

#include "ScreenShot.h"

#include <Client/IRenderer.h>
#include <Core/Bitmap.h>
#include <Core/Debug.h>
#include <Core/FileManager.h>
#include <Core/Math.h>
#include <Core/Settings.h>

DEFINE_SPADES_SETTING(cg_screenshotFormat, "jpeg");

namespace spades {
	namespace client {
		namespace {
			enum class ScreenshotFormat { JPG, TGA, PNG };

			ScreenshotFormat GetScreenshotFormat(const std::string& format) {
				if (EqualsIgnoringCase(format, "jpeg")) {
					return ScreenshotFormat::JPG;
				} else if (EqualsIgnoringCase(format, "tga")) {
					return ScreenshotFormat::TGA;
				} else if (EqualsIgnoringCase(format, "png")) {
					return ScreenshotFormat::PNG;
				} else {
					const auto& defaultValue = cg_screenshotFormat.GetDescriptor().defaultValue;
					SPLog("Invalid screenshot format: \"%s\", resetting to \"%s\"",
						format.c_str(), defaultValue.c_str());
					cg_screenshotFormat = defaultValue;
					return GetScreenshotFormat(defaultValue);
				}
			}

			std::string ScreenShotPath() {
				// One running counter for the whole process, so the client and the
				// editor share the Screenshots/ numbering and never rescan from zero.
				static int nextScreenShotIndex = 0;

				char bufJpg[32], bufTga[32], bufPng[32];
				const int maxShotIndex = 10000;
				for (int i = 0; i < maxShotIndex; i++) {
					snprintf(bufJpg, sizeof(bufJpg), "Screenshots/shot%04d.jpg", nextScreenShotIndex);
					snprintf(bufTga, sizeof(bufTga), "Screenshots/shot%04d.tga", nextScreenShotIndex);
					snprintf(bufPng, sizeof(bufPng), "Screenshots/shot%04d.png", nextScreenShotIndex);
					if (FileManager::FileExists(bufJpg) ||
						FileManager::FileExists(bufTga) ||
						FileManager::FileExists(bufPng)) {
						nextScreenShotIndex++;
						if (nextScreenShotIndex >= maxShotIndex)
							nextScreenShotIndex = 0;
						continue;
					}

					switch (GetScreenshotFormat(cg_screenshotFormat)) {
						case ScreenshotFormat::JPG: return bufJpg;
						case ScreenshotFormat::TGA: return bufTga;
						case ScreenshotFormat::PNG: return bufPng;
					}
					SPAssert(false);
				}

				SPRaise("No free file name");
			}
		} // namespace

		std::string SaveScreenShot(IRenderer& renderer) {
			Handle<Bitmap> bmp = renderer.ReadBitmap();
			std::string name = ScreenShotPath();
			bmp->Save(name);
			return name;
		}
	} // namespace client
} // namespace spades
