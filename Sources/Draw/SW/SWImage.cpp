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

#include "SWImage.h"
#include <Core/FileManager.h>
#include <Core/IStream.h>

namespace spades {
	namespace draw {
		namespace {
			inline uint32_t ProcessPixel(uint32_t col) {
				unsigned int alpha = static_cast<unsigned int>(col >> 24);
				alpha += (alpha >> 7); // [0,255] to [0,256]

				unsigned int r = static_cast<unsigned int>((col >> 0) & 0xFF);
				unsigned int g = static_cast<unsigned int>((col >> 8) & 0xFF);
				unsigned int b = static_cast<unsigned int>((col >> 16) & 0xFF);
				r = (r * alpha) >> 8;
				g = (g * alpha) >> 8;
				b = (b * alpha) >> 8;

				col &= 0xFF000000;
				col |= b | (g << 8) | (r << 16); // swap RGB/BGR
				return col;
			}

			inline int RoundUpToPowerOfTwo(int value) {
				int result = 1;
				while (result < value)
					result <<= 1;
				return result;
			}
		} // namespace

		SWImage::SWImage(Bitmap& m)
		    : ew(m.GetWidth()),
		      eh(m.GetHeight()),
		      pw(RoundUpToPowerOfTwo(m.GetWidth())),
		      ph(RoundUpToPowerOfTwo(m.GetHeight())),
		      isWhite(false),
		      w(static_cast<float>(m.GetWidth())),
		      h(static_cast<float>(m.GetHeight())),
		      iw(1.0F / static_cast<float>(pw)),
		      ih(1.0F / static_cast<float>(ph)) {
			// The padding stays fully transparent.
			bmp.assign(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph), 0);

			// premultiplied alpha
			{
				bool foundNonWhite = false;
				for (int y = 0; y < eh; ++y) {
					const uint32_t *inpix = m.GetPixels() + y * ew;
					uint32_t *outpix = bmp.data() + y * pw;
					for (int x = 0; x < ew; ++x) {
						uint32_t col = ProcessPixel(*(inpix++));
						*(outpix++) = col;
						if (col != 0xFFFFFFFF)
							foundNonWhite = true;
					}
				}
				// Only the exact area counts here, the padding is always transparent.
				isWhite = !foundNonWhite;
			}
		}

		SWImage::SWImage(int w, int h)
		    : ew(w),
		      eh(h),
		      pw(RoundUpToPowerOfTwo(w)),
		      ph(RoundUpToPowerOfTwo(h)),
		      isWhite(false),
		      w(static_cast<float>(ew)),
		      h(static_cast<float>(eh)),
		      iw(1.0F / static_cast<float>(pw)),
		      ih(1.0F / static_cast<float>(ph)) {
			bmp.resize(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph));
		}

		SWImage::~SWImage() {}

		void SWImage::Update(Bitmap& inBmp, int x, int y) {
			SPADES_MARK_FUNCTION();
			if (x < 0 || y < 0 || x + inBmp.GetWidth() > ew || y + inBmp.GetHeight() > eh)
				SPRaise("Out of range.");

			{
				int bw = inBmp.GetWidth();
				int bh = inBmp.GetHeight();
				for (int yy = 0; yy < bh; ++yy) {
					uint32_t* inpix = inBmp.GetPixels() + yy * bw;
					uint32_t* outpix = bmp.data() + x + (y + yy) * pw;
					for (int j = bw; j; --j)
						*(outpix++) = ProcessPixel(*(inpix++));
				}
			}

			isWhite = false;
		}

		SWImageManager::~SWImageManager() {}

		Handle<SWImage> SWImageManager::RegisterImage(const std::string& name) {
			// The default white image is built in memory. The legacy file name is
			// kept as an alias so existing callers and mods keep working.
			if (IsBuiltIn(name))
				return GetWhiteImage();

			auto it = images.find(name);
			if (it == images.end()) {
				Handle<Bitmap> bitmap = Bitmap::Load(name);
				Handle<SWImage> image = CreateImage(*bitmap);
				images.insert(std::make_pair(name, image));
				return image;
			} else {
				return it->second;
			}
		}

		Handle<SWImage> SWImageManager::GetWhiteImage() {
			if (!whiteImage) {
				// Build the 1x1 white default image in memory.
				Handle<Bitmap> bmp = Handle<Bitmap>::New(1, 1);
				bmp->SetPixel(0, 0, 0xFFFFFFFF);
				whiteImage = CreateImage(*bmp);
			}
			return whiteImage;
		}

		Handle<SWImage> SWImageManager::CreateImage(Bitmap& bitmap) {
			return Handle<SWImage>::New(bitmap);
		}

		bool SWImageManager::IsBuiltIn(const std::string& name) {
			return name == "*white" || name == "Gfx/White.tga";
		}

		void SWImageManager::ClearCache() { images.clear(); }

		std::vector<std::string> SWImageManager::GetFiles() const {
			std::vector<std::string> files;
			for (const auto& entry : images)
				files.push_back(entry.first);
			return files;
		}
	} // namespace draw
} // namespace spades
