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

#include <list>
#include <memory>
#include <unordered_map>

#include <Client/IFont.h>
#include <Core/TMPUtils.h>

struct FT_FaceRec_;
typedef struct FT_FaceRec_ *FT_Face;

namespace spades {
	class Bitmap;
}

namespace spades {
	namespace client {
		class IImage;
	}
} // namespace spades

namespace spades {
	namespace ngclient {
		struct FTFaceWrapper;
		class FTFont;

		class FTFontSet {
			friend class FTFont;
			std::list<std::unique_ptr<FTFaceWrapper>> faces;

		public:
			FTFontSet();
			FTFontSet(const FTFontSet &) = delete;
			void operator=(const FTFontSet &) = delete;
			~FTFontSet();

			void AddFace(const std::string &fileName);
		};

		/**
		 * FreeType2 based font renderer.
		 *
		 * Its sizes and measures are in the renderer's 2D units, but its glyphs
		 * are rasterized at the display's pixel density (IRenderer::
		 * ScreenPixelRatio): a high-DPI display gets text as sharp as its
		 * pixels, not glyphs of 2D units stretched over several of them. The
		 * glyphs are rasterized again should the density change (the window
		 * moved to another display).
		 *
		 * Warning: only one thread can access multiple FTFonts sharing the same FTFontSet
		 *			at the same time.
		 */
		class FTFont : public client::IFont {
			Handle<client::IRenderer> renderer;

			struct GlyphImage {
				client::IImage &img;
				AABB2 const bounds;
				Vector2 const offset;
				GlyphImage(client::IImage &img, const AABB2 &bounds, const Vector2 &offset)
					: img(img), bounds(bounds), offset(offset) {}
			};

			struct Glyph {
				FT_Face face;
				uint32_t charIndex;
				Vector2 advance;
				stmp::optional<GlyphImage> image;
				stmp::optional<GlyphImage> blurImage;
				stmp::optional<GlyphImage> outlineImage;
				Handle<Bitmap> bmp;
			};

			struct GlyphHash {
				std::size_t operator()(const std::pair<FT_Face, uint32_t> &p) const {
					return std::hash<FT_Face>()(p.first) ^ std::hash<uint32_t>()(p.second);
				}
			};

			std::unordered_map<std::pair<FT_Face, uint32_t>, Glyph, GlyphHash> glyphs;
			std::unordered_map<uint32_t, std::reference_wrapper<Glyph>> glyphMap;
			float lineHeight;
			/** em height */
			float height;
			float baselineY;
			/** Pixels a glyph is rasterized with per 2D unit. */
			float density;

			bool rendererIsLowQuality;

			std::shared_ptr<FTFontSet> fontSet;

			struct Bin;
			std::list<Bin> bins;
			int binSize;

			stmp::optional<Glyph &> GetGlyph(uint32_t code);
			template <class T, class T2, class T3>
			void SplitTextIntoGlyphs(const std::string &, T glyphHandler, T3 fallbackHandler,
									 T2 lineBreakHandler);

			/** Follows the display's pixel density, rasterizing the glyphs
			 *  again when it changed. */
			void UpdateDensity();
			/** `target` (2D units) on the nearest pixel, so glyphs stay crisp. */
			Vector2 SnapToPixel(const Vector2 &target) const;
			void RenderGlyph(Glyph &);
			void RenderBlurGlyph(Glyph &);
			/** `bmp` blurred once, grown by the kernel's reach on every side. */
			static Handle<Bitmap> BlurOnce(Bitmap &bmp);
			void RenderOutlineGlyph(Glyph &);

		protected:
			~FTFont();

		public:
			FTFont(client::IRenderer *, std::shared_ptr<FTFontSet>, float height, float lineHeight);

			Vector2 Measure(const std::string &) override;

			float GetHeight() const { return height; }
			float GetLineHeight() const { return lineHeight; }

			void Draw(const std::string &, Vector2 offset, float scale, Vector4 color) override;
			void DrawBlurred(const std::string &, Vector2 offset, float scale, Vector4 color);
			void DrawOutlined(const std::string &, Vector2 offset, float scale, Vector4 color);
			void DrawShadow(const std::string &, const Vector2 &offset, float scale,
							const Vector4 &color, const Vector4 &shadowColor) override;
			void DrawOutline(const std::string &, const Vector2 &offset, float scale,
							const Vector4 &color, const Vector4 &shadowColor) override;
		};
	} // namespace ngclient
} // namespace spades
