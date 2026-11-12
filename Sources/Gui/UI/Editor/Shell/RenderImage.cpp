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

#include "RenderImage.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <Core/Bitmap.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>

namespace spades {
	namespace gui {
		namespace {
			// A pixel is four bytes, red first, whatever the machine's byte order.
			struct Pixel {
				int r, g, b;
			};
			Pixel Read(const std::uint32_t* p) {
				const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(p);
				return {bytes[0], bytes[1], bytes[2]};
			}
			void Write(std::uint32_t* p, int r, int g, int b, int a) {
				std::uint8_t* bytes = reinterpret_cast<std::uint8_t*>(p);
				bytes[0] = std::uint8_t(r);
				bytes[1] = std::uint8_t(g);
				bytes[2] = std::uint8_t(b);
				bytes[3] = std::uint8_t(a);
			}
			int Difference(const Pixel& a, const Pixel& b) {
				return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
			}
			int Clamp(float v) { return std::max(0, std::min(255, int(v + 0.5F))); }

			// Pixels differing by less than this are the subject, give or take
			// what a renderer does not draw twice alike; with none differing by
			// more, no background shows and the picture is all subject.
			constexpr int kLeastSpread = 24;
			// Coverage this close to all or nothing is noise of the renderer's,
			// not an edge.
			constexpr float kSnap = 0.06F;
		} // namespace

		Handle<Bitmap> LiftSubject(Bitmap& onFirst, Bitmap& onSecond, std::uint32_t background,
		                           bool transparent) {
			const int width = onFirst.GetWidth();
			const int height = onFirst.GetHeight();
			if (onSecond.GetWidth() != width || onSecond.GetHeight() != height)
				SPRaise("The two pictures differ in size");
			const std::size_t count = std::size_t(width) * std::size_t(height);
			const std::uint32_t* first = onFirst.GetPixels();
			const std::uint32_t* second = onSecond.GetPixels();

			// Bare background differs by the same amount wherever it shows, so
			// that amount is the one most pixels that differ at all differ by:
			// it says how far apart the two backgrounds came out. Not the
			// greatest difference, which a filter sharpening the model's edge
			// overshoots.
			constexpr int kMostDifference = 3 * 255;
			std::vector<std::size_t> pixelsDifferingBy(kMostDifference + 1, 0);
			for (std::size_t i = 0; i < count; i++)
				pixelsDifferingBy[std::size_t(Difference(Read(first + i), Read(second + i)))]++;
			int spread = 0;
			std::size_t most = 0;
			for (int d = kLeastSpread; d <= kMostDifference; d++) {
				if (pixelsDifferingBy[std::size_t(d)] > most) {
					most = pixelsDifferingBy[std::size_t(d)];
					spread = d;
				}
			}
			// And what the first background was: those pixels' colour.
			double sumR = 0.0, sumG = 0.0, sumB = 0.0;
			std::size_t bareCount = 0;
			for (std::size_t i = 0; spread > 0 && i < count; i++) {
				const Pixel a = Read(first + i);
				if (Difference(a, Read(second + i)) != spread)
					continue;
				sumR += a.r;
				sumG += a.g;
				sumB += a.b;
				bareCount++;
			}
			Pixel bare = {0, 0, 0};
			if (bareCount > 0)
				bare = {int(sumR / double(bareCount)), int(sumG / double(bareCount)),
				        int(sumB / double(bareCount))};

			const int backR = int(background & 0xFF);
			const int backG = int((background >> 8) & 0xFF);
			const int backB = int((background >> 16) & 0xFF);

			auto out = Handle<Bitmap>::New(width, height);
			std::uint32_t* pixels = out->GetPixels();
			for (std::size_t i = 0; i < count; i++) {
				const Pixel a = Read(first + i);
				float cover = 1.0F; // how much of the pixel is subject
				if (spread > 0)
					cover = 1.0F - float(Difference(a, Read(second + i))) / float(spread);
				if (cover < kSnap)
					cover = 0.0F;
				else if (cover > 1.0F - kSnap)
					cover = 1.0F;

				// The subject's own colour, with the background it was drawn
				// over taken back out of an edge.
				float r = float(a.r), g = float(a.g), b = float(a.b);
				if (cover > 0.0F && cover < 1.0F) {
					r = (r - (1.0F - cover) * float(bare.r)) / cover;
					g = (g - (1.0F - cover) * float(bare.g)) / cover;
					b = (b - (1.0F - cover) * float(bare.b)) / cover;
				}
				if (transparent) {
					if (cover <= 0.0F)
						Write(pixels + i, 0, 0, 0, 0);
					else
						Write(pixels + i, Clamp(r), Clamp(g), Clamp(b), Clamp(cover * 255.0F));
				} else {
					Write(pixels + i, Clamp(r * cover + float(backR) * (1.0F - cover)),
					      Clamp(g * cover + float(backG) * (1.0F - cover)),
					      Clamp(b * cover + float(backB) * (1.0F - cover)), 255);
				}
			}
			return out;
		}

		std::string SaveRenderImage(Bitmap& image) {
			// One running counter for the process, as the screenshots keep.
			static int next = 0;
			const int most = 10000;
			for (int tried = 0; tried < most; tried++) {
				char name[40];
				std::snprintf(name, sizeof(name), "Screenshots/render%04d.png", next);
				next = (next + 1) % most;
				if (FileManager::FileExists(name))
					continue;
				image.Save(name);
				return name;
			}
			SPRaise("No free file name");
		}
	} // namespace gui
} // namespace spades
