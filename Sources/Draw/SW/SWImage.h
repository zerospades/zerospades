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

#pragma once

#include <unordered_map>

#include <Client/IImage.h>
#include <Core/Bitmap.h>
#include <Core/Math.h>

namespace spades {
	namespace draw {
		class SWImage : public client::IImage {
			std::vector<uint32_t> bmp;
			int ew, eh; // exact size
			int pw, ph; // size of the pixel buffer, rounded up to a power of two

			bool isWhite;

			float w, h;
			float iw, ih; // 1 / pw and 1 / ph, not the inverse of the exact size

		protected:
			~SWImage();

		public:
			SWImage(Bitmap& bmp);
			SWImage(int w, int h);

			uint32_t* GetRawBitmap() { return bmp.data(); }

			/**
			 * Size of the pixel buffer. This is always a power of two and also the row
			 * stride, so it can be larger than `GetWidth()` and `GetHeight()`.
			 */
			int GetRawWidth() { return pw; }
			int GetRawHeight() { return ph; }

			bool IsWhiteImage() { return isWhite; }

			void Update(Bitmap&, int x, int y) override;

			float GetWidth() override { return w; }
			float GetHeight() override { return h; }

			/** Inverse of the buffer size, to convert pixel coordinates to UV. */
			float GetInvWidth() { return iw; }
			float GetInvHeight() { return ih; }

			/**
			 * UV of the bottom-right corner of the image. (1, 1) would point into
			 * the padding when the image size is not a power of two.
			 */
			Vector2 GetUVScale() { return MakeVector2(w * iw, h * ih); }
		};

		class SWImageManager {
			std::unordered_map<std::string, Handle<SWImage>> images;
			Handle<SWImage> whiteImage;

		public:
			SWImageManager() {}
			~SWImageManager();

			Handle<SWImage> RegisterImage(const std::string&);
			Handle<SWImage> CreateImage(Bitmap&);
			Handle<SWImage> GetWhiteImage();
			/** Whether `RegisterImage` makes `name` in memory rather than load it. */
			static bool IsBuiltIn(const std::string& name);

			void ClearCache();
			/** The files the cached ones were loaded from. */
			std::vector<std::string> GetFiles() const;
		};
	} // namespace draw
} // namespace spades
