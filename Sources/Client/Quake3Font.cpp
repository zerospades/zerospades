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

#include "Quake3Font.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "IRenderer.h"
#include <Core/Debug.h>

namespace spades {
	namespace client {
		namespace {
			constexpr int kOutlineThickness = 1;
		} // namespace

		Quake3Font::Quake3Font(IRenderer* r, IImage* tex, Bitmap* sourceBitmap, const int* mp,
		                       int gh, float sw, bool extended)
		    : IFont(r), renderer(r), tex(tex), glyphHeight(gh), spaceWidth(sw), source(sourceBitmap) {
			SPADES_MARK_FUNCTION();

			tex->AddRef();
			for (int i = 0; i < (extended ? 256 : 128); i++) {
				int x = *(mp++);
				int y = *(mp++);
				int w = *(mp++);
				int adv;
				if (extended) {
					adv = *(mp++);
				} else {
					adv = w;
				}

				GlyphInfo info;
				if (w == -1) {
					info.type = Invalid;
					glyphs.push_back(info);
					continue;
				}
				if (adv == PROP_SPACE_WIDTH) {
					info.type = Space;
					glyphs.push_back(info);
					continue;
				}

				info.type = Image;
				info.imageRect = AABB2(
					static_cast<float>(x),
					static_cast<float>(y),
					static_cast<float>(w),
					static_cast<float>(gh)
				);
				info.advance = static_cast<float>(adv);
				info.srcX = x;
				info.srcY = y;
				info.srcW = w;

				glyphs.push_back(info);
			}

			yMin = 0.0F;
			yMax = static_cast<float>(gh);

			// Build the outline atlas up front so the first outlined draw does not pay for it.
			BuildOutline();
		}

		Quake3Font::~Quake3Font() {
			SPADES_MARK_FUNCTION();
			tex->Release();
		}

		void Quake3Font::SetGlyphYRange(float yMin, float yMax) {
			this->yMin = yMin;
			this->yMax = yMax;
		}

		Vector2 Quake3Font::Measure(const std::string& txt) {
			SPADES_MARK_FUNCTION();
			SyncTextureScale();

			float x = 0.0F, w = 0.0F, h = static_cast<float>(glyphHeight);
			for (size_t i = 0; i < txt.size();) {
				size_t chrLen = 0;
				uint32_t ch = GetCodePointFromUTF8String(txt, i, &chrLen);
				SPAssert(chrLen > 0);
				i += chrLen;
				if (ch >= static_cast<uint32_t>(glyphs.size()))
					goto fallback;

				if (ch == 13 || ch == 10) {
					// new line
					x = 0.0F;
					h += static_cast<float>(glyphHeight);
					continue;
				}

				{
					const GlyphInfo& info = glyphs[ch];

					if (info.type == Invalid)
						goto fallback;
					else if (info.type == Space)
						x += spaceWidth;
					else if (info.type == Image)
						x += info.advance;

					if (x > w)
						w = x;
				}

				continue;
			fallback:
				// As Draw() lays it out: at the fitted size, in unfitted units.
				x += MeasureFallback(ch, (yMax - yMin) * pixelFit) / pixelFit;
				if (x > w)
					w = x;
			}

			return MakeVector2(w, h) * pixelFit;
		}

		void Quake3Font::Draw(const std::string& txt, spades::Vector2 offset, float scale,
		                      spades::Vector4 color) {
			float x = 0.0F, y = 0.0F;

			if (scale == 1.0F)
				offset = SnapOrigin(offset);

			SyncTextureScale();
			scale *= pixelFit;
			IImage* image = scaledTex ? scaledTex.GetPointerOrNull() : tex;
			const float texelScale = scaledTex ? static_cast<float>(texScale) : 1.0F;

			float a = color.w;
			color.w = 1.0F;
			color *= a;
			renderer->SetColorAlphaPremultiplied(color);

			float invScale = 1.0F / scale;

			for (size_t i = 0; i < txt.size();) {
				size_t chrLen = 0;
				uint32_t ch = GetCodePointFromUTF8String(txt, i, &chrLen);
				SPAssert(chrLen > 0);
				i += chrLen;
				if (ch >= static_cast<uint32_t>(glyphs.size()))
					goto fallback;

				if (ch == 13 || ch == 10) {
					// new line
					x = 0.0F;
					y += static_cast<float>(glyphHeight);
					continue;
				}

				{
					const GlyphInfo& info = glyphs[ch];

					if (info.type == Invalid)
						goto fallback;
					else if (info.type == Space) {
						x += spaceWidth;
					} else if (info.type == Image) {
						AABB2 rt(x * scale + offset.x, y * scale + offset.y,
						         info.imageRect.GetWidth() * scale,
						         info.imageRect.GetHeight() * scale);
						const AABB2& src = info.imageRect;
						renderer->DrawImage(image, rt,
						                    AABB2(src.min.x * texelScale, src.min.y * texelScale,
						                          src.GetWidth() * texelScale,
						                          src.GetHeight() * texelScale));
						x += info.advance;
					}
				}
				continue;
			fallback:
				DrawFallback(ch, MakeVector2(x, y + yMin) * scale + offset, (yMax - yMin) * scale,
				             color);
				x += MeasureFallback(ch, (yMax - yMin) * scale) * invScale;
			}
		}

		void Quake3Font::BuildOutline() {
			const int t = kOutlineThickness;
			const int srcBmpW = source->GetWidth();
			const int srcBmpH = source->GetHeight();
			const int cellH = glyphHeight + t * 2;

			// lay out one padded cell per glyph. All cells share the same height,
			// so a simple row-by-row packing is enough. A 1 pixel gap between cells keeps
			// bilinear filtering from bleeding neighbouring glyphs into each other.
			int maxCellW = 0;
			for (const GlyphInfo& g : glyphs) {
				if (g.type == Image)
					maxCellW = std::max(maxCellW, g.srcW + t * 2);
			}
			if (maxCellW == 0)
				return;

			int atlasW = 256;
			while (atlasW < maxCellW)
				atlasW <<= 1;

			const int gap = 1;
			std::vector<int> posX(glyphs.size(), 0), posY(glyphs.size(), 0);
			int curX = 0, curY = 0;
			for (size_t i = 0; i < glyphs.size(); i++) {
				if (glyphs[i].type != Image)
					continue;
				int cw = glyphs[i].srcW + t * 2;
				if (curX + cw > atlasW) {
					curX = 0;
					curY += cellH + gap;
				}
				posX[i] = curX;
				posY[i] = curY;
				curX += cw + gap;
			}

			int atlasH = 64;
			while (atlasH < curY + cellH)
				atlasH <<= 1;

			// dilate each glyph (max of alpha over a square kernel). The kernel is
			// clipped to the glyph cell so neighbouring glyphs never bleed into each other.
			Handle<Bitmap> atlas = Handle<Bitmap>::New(atlasW, atlasH);
			uint32_t* dst = atlas->GetPixels();
			std::memset(dst, 0, static_cast<size_t>(atlasW) * static_cast<size_t>(atlasH) * 4);
			const uint32_t* srcPixels = source->GetPixels();

			for (size_t i = 0; i < glyphs.size(); i++) {
				GlyphInfo& g = glyphs[i];
				if (g.type != Image)
					continue;

				const int cw = g.srcW + t * 2;
				for (int oy = 0; oy < cellH; oy++) {
					for (int ox = 0; ox < cw; ox++) {
						// Pixel position relative to the top-left corner of the glyph itself.
						const int cx = ox - t;
						const int cy = oy - t;

						uint32_t maxAlpha = 0;
						for (int ky = cy - t; ky <= cy + t; ky++) {
							if (ky < 0 || ky >= glyphHeight)
								continue;
							const int py = g.srcY + ky;
							if (py < 0 || py >= srcBmpH)
								continue;
							for (int kx = cx - t; kx <= cx + t; kx++) {
								if (kx < 0 || kx >= g.srcW)
									continue;
								const int px = g.srcX + kx;
								if (px < 0 || px >= srcBmpW)
									continue;
								maxAlpha = std::max(maxAlpha, srcPixels[px + py * srcBmpW] >> 24);
							}
						}

						if (maxAlpha > 0)
							dst[(posY[i] + oy) * atlasW + posX[i] + ox] = (maxAlpha << 24) | 0xFFFFFF;
					}
				}

				g.outlineRect = AABB2(
					static_cast<float>(posX[i]),
					static_cast<float>(posY[i]),
					static_cast<float>(cw),
					static_cast<float>(cellH)
				);
			}

			outlineImage = renderer->CreateImage(*atlas);
			outlineAtlas = atlas;
		}

		namespace {
			Handle<Bitmap> UpscaleNearest(Bitmap& src, int k) {
				const int w = src.GetWidth(), h = src.GetHeight();
				auto out = Handle<Bitmap>::New(w * k, h * k);
				const uint32_t* in = src.GetPixels();
				uint32_t* o = out->GetPixels();
				for (int y = 0; y < h * k; y++)
					for (int x = 0; x < w * k; x++)
						*(o++) = in[(x / k) + (y / k) * w];
				return out;
			}
		} // namespace

		void Quake3Font::SyncTextureScale() {
			const float s = renderer->GetUIScale();
			int k = std::max(1, static_cast<int>(std::ceil(s - 0.001F)));
			float fit = 1.0F;
			if (s > 1.0F && s / static_cast<float>(k) < 0.71F) {
				k = std::max(1, static_cast<int>(std::floor(s + 0.001F)));
				fit = static_cast<float>(k) / s;
			}
			k = std::min(k, 4);
			pixelFit = fit;
			if (k == texScale)
				return;
			texScale = k;
			scaledTex = nullptr;
			scaledOutlineImage = nullptr;
			if (k > 1) {
				if (source)
					scaledTex = renderer->CreateImage(*UpscaleNearest(*source, k));
				if (outlineAtlas)
					scaledOutlineImage = renderer->CreateImage(*UpscaleNearest(*outlineAtlas, k));
			}
		}

		Vector2 Quake3Font::SnapOrigin(Vector2 offset) const {
			float s = renderer->GetUIScale();
			return (offset * s).Floor() * (1.0F / s);
		}

		void Quake3Font::DrawOutlined(const std::string& txt, Vector2 offset, float scale, Vector4 color) {
			float x = 0.0F, y = 0.0F;

			// Must match the rounding done in Draw() so the outline stays aligned with the text.
			if (scale == 1.0F)
				offset = SnapOrigin(offset);

			SyncTextureScale();
			scale *= pixelFit;
			IImage* image = scaledOutlineImage ? scaledOutlineImage.GetPointerOrNull()
			                                   : outlineImage.GetPointerOrNull();
			const float texelScale = scaledOutlineImage ? static_cast<float>(texScale) : 1.0F;

			float a = color.w;
			color.w = 1.0F;
			color *= a;
			renderer->SetColorAlphaPremultiplied(color);

			const float invScale = 1.0F / scale;
			const float margin = static_cast<float>(kOutlineThickness) * scale;

			for (size_t i = 0; i < txt.size();) {
				size_t chrLen = 0;
				uint32_t ch = GetCodePointFromUTF8String(txt, i, &chrLen);
				SPAssert(chrLen > 0);
				i += chrLen;

				if (ch == 13 || ch == 10) {
					x = 0.0F;
					y += static_cast<float>(glyphHeight);
					continue;
				}

				if (ch < static_cast<uint32_t>(glyphs.size())) {
					const GlyphInfo& info = glyphs[ch];
					if (info.type == Space) {
						x += spaceWidth;
						continue;
					}
					if (info.type == Image) {
						AABB2 rt(x * scale + offset.x - margin, y * scale + offset.y - margin,
						         info.outlineRect.GetWidth() * scale,
						         info.outlineRect.GetHeight() * scale);
						const AABB2& src = info.outlineRect;
						renderer->DrawImage(image, rt,
						                    AABB2(src.min.x * texelScale, src.min.y * texelScale,
						                          src.GetWidth() * texelScale,
						                          src.GetHeight() * texelScale));
						x += info.advance;
						continue;
					}
				}

				x += MeasureFallback(ch, (yMax - yMin) * scale) * invScale;
			}
		}

		void Quake3Font::DrawOutline(const std::string& txt, const Vector2& offset, float scale,
		                             const Vector4& color, const Vector4& outlineColor) {
			DrawOutlined(txt, offset, scale, outlineColor);
			Draw(txt, offset, scale, color);
		}
	} // namespace client
} // namespace spades
