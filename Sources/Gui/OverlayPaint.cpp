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

#include "OverlayPaint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <Client/IImage.h>
#include <Client/IRenderer.h>

namespace spades {
    namespace gui {
        namespace {
            // Points closer than this (in 2D units) are one point: a zero-length
            // edge has no direction to take a normal from.
            constexpr float kPolygonEpsilon = 1.0e-4F;

            // At a corner the fringe is pushed out along the mitre, which grows as
            // the corner sharpens. Cap the stretch (it scales 1/|m|^2, so a corner
            // vertex moves at most twice as far as an edge does) so a needle-thin
            // corner doesn't throw a long faint spike.
            constexpr float kMaxMiterScale = 4.0F;

            // One framebuffer pixel, in 2D units.
            float PixelSize(client::IRenderer& renderer) {
                const float ratio = renderer.ScreenPixelRatio();
                return ratio > 0.0F ? 1.0F / ratio : 1.0F;
            }

            Vector4 Premultiply(const Vector4& c) {
                return MakeVector4(c.x * c.w, c.y * c.w, c.z * c.w, c.w);
            }

            // Segments per circle, by radius: smooth at any size, bounded cost.
            std::size_t CircleSegments(float radius) {
                return std::size_t(std::max(12, std::min(64, int(radius * 0.75F))));
            }

            // Polygons up to this many points are worked on without touching the
            // heap; every overlay shape drawn per frame (lines, facets, circles, a
            // gizmo ring) fits.
            constexpr std::size_t kInlinePolygonPoints = 128;

            // Working storage for one polygon's points: on the stack for the small
            // shapes drawn every frame, on the heap only for a larger one.
            class PointScratch {
            public:
                explicit PointScratch(std::size_t capacity) {
                    if (capacity > local.size()) {
                        heap.resize(capacity);
                        data = heap.data();
                    } else {
                        data = local.data();
                    }
                }
                PointScratch(const PointScratch&) = delete;
                PointScratch& operator=(const PointScratch&) = delete;

                Vector2& operator[](std::size_t i) { return data[i]; }
                const Vector2& operator[](std::size_t i) const { return data[i]; }
                Vector2* Data() { return data; }

            private:
                std::array<Vector2, kInlinePolygonPoints> local;
                std::vector<Vector2> heap;
                Vector2* data;
            };

            // Copies `points`, dropping each one that repeats the one before it,
            // and returns how many are left.
            std::size_t CopyDistinct(const Vector2* points, std::size_t count, PointScratch& out) {
                std::size_t n = 0;
                for (std::size_t i = 0; i < count; i++) {
                    if (n == 0 || (points[i] - out[n - 1]).GetLength() > kPolygonEpsilon)
                        out[n++] = points[i];
                }
                return n;
            }

            // Twice the signed area of the polygon `points`: positive when they run
            // counter-clockwise in a y-up frame.
            float SignedArea2(const Vector2* points, std::size_t n) {
                float area2 = 0.0F;
                for (std::size_t i = 0; i < n; i++) {
                    const Vector2& p = points[i];
                    const Vector2& q = points[(i + 1) % n];
                    area2 += p.x * q.y - q.x * p.y;
                }
                return area2;
            }

            // `count` points evenly round a circle.
            void CirclePoints(const Vector2& center, float radius, std::size_t count,
                              PointScratch& out) {
                const float step = 2.0F * M_PI_F / float(count);
                for (std::size_t i = 0; i < count; i++) {
                    const float a = float(i) * step;
                    out[i] = center + MakeVector2(std::cos(a), std::sin(a)) * radius;
                }
            }

            // A quad fading from `solid` along the edge solid0-solid1 to nothing
            // along clear0-clear1, where clear0 lies across from solid0.
            void FadeQuad(client::IRenderer& renderer, const Vector2& solid0,
                          const Vector2& solid1, const Vector2& clear0, const Vector2& clear1,
                          const Vector4& solid) {
                const Vector4 clear = MakeVector4(0.0F, 0.0F, 0.0F, 0.0F);
                renderer.DrawShadedTriangle(solid0, solid1, clear1, solid, solid, clear);
                renderer.DrawShadedTriangle(solid0, clear1, clear0, solid, clear, clear);
            }

            // Draws a polygon given as its solid core (`inner`) and the outline its
            // anti-aliased rim fades out to (`outer`), point for point: the core as
            // a fan, then each edge's rim as a quad fading from `solid` to nothing.
            void DrawFringedPolygon(client::IRenderer& renderer, const Vector2* inner,
                                    const Vector2* outer, std::size_t n, const Vector4& solid) {
                for (std::size_t i = 1; i + 1 < n; i++)
                    renderer.DrawShadedTriangle(inner[0], inner[i], inner[i + 1], solid, solid,
                                                solid);

                for (std::size_t i = 0; i < n; i++) {
                    const std::size_t j = (i + 1) % n;
                    FadeQuad(renderer, inner[i], inner[j], outer[i], outer[j], solid);
                }
            }
        } // namespace

        void OverlayColorNP(client::IRenderer& renderer, const Vector4& c) {
            renderer.SetColorAlphaPremultiplied(Premultiply(c));
        }
        void OverlayFillRect(client::IRenderer& renderer, float x, float y, float w, float h) {
            renderer.DrawImage((client::IImage*)NULL, AABB2(x, y, w, h));
        }
        void OverlayStrokeRect(client::IRenderer& renderer, float x, float y, float w, float h,
                               float t, const Vector4& c) {
            OverlayColorNP(renderer, c);
            OverlayFillRect(renderer, x, y, w, t);
            OverlayFillRect(renderer, x, y + h - t, w, t);
            OverlayFillRect(renderer, x, y, t, h);
            OverlayFillRect(renderer, x + w - t, y, t, h);
        }
        void OverlayFillConvexPolygon(client::IRenderer& renderer, const Vector2* points,
                                      std::size_t count, const Vector4& c) {
            if (c.w <= 0.0F)
                return;

            PointScratch pts(count);
            std::size_t n = CopyDistinct(points, count, pts);
            while (n > 1 && (pts[n - 1] - pts[0]).GetLength() <= kPolygonEpsilon)
                n--;
            if (n < 3)
                return;

            // Twice the signed area gives the winding, so normals can be made to
            // point outwards whichever way the caller listed the points.
            const float area2 = SignedArea2(pts.Data(), n);
            if (std::fabs(area2) <= kPolygonEpsilon)
                return;
            const float orient = area2 > 0.0F ? 1.0F : -1.0F;

            // Outward unit normal of edge i (from point i to point i + 1).
            PointScratch edgeNormal(n);
            for (std::size_t i = 0; i < n; i++) {
                const Vector2 d = pts[(i + 1) % n] - pts[i];
                edgeNormal[i] = MakeVector2(d.y, -d.x) * (orient / d.GetLength());
            }

            // Each vertex moves half a pixel in (solid core) and half a pixel out
            // (transparent rim) along the mitre of its two edges, scaled so both
            // edges end up exactly half a pixel from where they were.
            const float half = PixelSize(renderer) * 0.5F;
            PointScratch inner(n), outer(n);
            for (std::size_t i = 0; i < n; i++) {
                Vector2 m = (edgeNormal[(i + n - 1) % n] + edgeNormal[i]) * 0.5F;
                const float len2 = m.x * m.x + m.y * m.y;
                if (len2 > kPolygonEpsilon)
                    m *= std::min(1.0F / len2, kMaxMiterScale);
                inner[i] = pts[i] - m * half;
                outer[i] = pts[i] + m * half;
            }

            // A polygon under a pixel across has no room for a core: moving its
            // sides half a pixel in turns the inner outline inside out, which
            // would cover far more than the shape. Shrink the core to the centre
            // instead, and fade the colour so the ink laid down (a cone over the
            // outer outline holds a third of its area) still matches the shape's.
            Vector4 col = c;
            bool collapsed = SignedArea2(inner.Data(), n) * orient <= 0.0F;
            for (std::size_t i = 0; i < n && !collapsed; i++) {
                const std::size_t j = (i + 1) % n;
                collapsed = Vector2::Dot(inner[j] - inner[i], pts[j] - pts[i]) <= 0.0F;
            }
            if (collapsed) {
                Vector2 centre = MakeVector2(0.0F, 0.0F);
                for (std::size_t i = 0; i < n; i++)
                    centre += pts[i];
                centre *= 1.0F / float(n);
                for (std::size_t i = 0; i < n; i++)
                    inner[i] = centre;
                const float outerArea2 = std::fabs(SignedArea2(outer.Data(), n));
                if (outerArea2 > kPolygonEpsilon)
                    col.w *= std::min(1.0F, 3.0F * std::fabs(area2) / outerArea2);
            }

            DrawFringedPolygon(renderer, inner.Data(), outer.Data(), n, Premultiply(col));
        }

        void OverlayStrokeLine(client::IRenderer& renderer, const Vector2& a, const Vector2& b,
                               float width, const Vector4& c) {
            const Vector2 ends[2] = {a, b};
            OverlayStrokePolyline(renderer, ends, 2, width, c, false);
        }

        void OverlayStrokePolyline(client::IRenderer& renderer, const Vector2* points,
                                   std::size_t count, float width, const Vector4& c,
                                   bool closed) {
            if (width <= 0.0F || c.w <= 0.0F)
                return;

            PointScratch pts(count);
            std::size_t n = CopyDistinct(points, count, pts);
            if (closed) {
                while (n > 1 && (pts[n - 1] - pts[0]).GetLength() <= kPolygonEpsilon)
                    n--;
            }
            if (n < 2)
                return;
            if (n < 3)
                closed = false; // two points close into the one segment between them

            Vector4 col = c;
            const float pixel = PixelSize(renderer);
            if (width < pixel) {
                col.w *= width / pixel;
                width = pixel;
            }
            const float half = pixel * 0.5F;
            // The core reaches half a pixel short of each side of the stroke, the
            // rim half a pixel past it.
            const float innerReach = std::max(width * 0.5F - half, 0.0F);
            const float outerReach = width * 0.5F + half;

            // Left-hand unit normal of segment s (from point s to point s + 1).
            const std::size_t segments = closed ? n : n - 1;
            PointScratch normal(segments);
            for (std::size_t s = 0; s < segments; s++) {
                const Vector2 d = pts[(s + 1) % n] - pts[s];
                normal[s] = MakeVector2(-d.y, d.x) * (1.0F / d.GetLength());
            }

            // Each point is offset along the mitre of the segments meeting there, so
            // neighbouring segments share the edge across their joint and no pixel
            // is covered twice. An open end is squared off instead: the core stops
            // half a pixel inside it (never past the segment's middle) and the rim
            // half a pixel beyond it.
            PointScratch innerL(n), innerR(n), outerL(n), outerR(n);
            for (std::size_t i = 0; i < n; i++) {
                Vector2 m;
                Vector2 innerShift = MakeVector2(0.0F, 0.0F);
                Vector2 outerShift = MakeVector2(0.0F, 0.0F);
                if (!closed && (i == 0 || i == n - 1)) {
                    const std::size_t s = (i == 0) ? 0 : segments - 1;
                    m = normal[s];
                    // Unit direction pointing from this end into the stroke.
                    const Vector2 inward = MakeVector2(m.y, -m.x) * ((i == 0) ? 1.0F : -1.0F);
                    const float length = (pts[s + 1] - pts[s]).GetLength();
                    innerShift = inward * std::min(half, length * 0.5F);
                    outerShift = inward * -half;
                } else {
                    const Vector2& before = normal[(i + n - 1) % n];
                    const Vector2& after = normal[i];
                    m = (before + after) * 0.5F;
                    const float len2 = m.x * m.x + m.y * m.y;
                    if (len2 > kPolygonEpsilon)
                        m *= std::min(1.0F / len2, kMaxMiterScale);
                    else
                        m = after; // the path doubles back on itself
                }
                innerL[i] = pts[i] + innerShift + m * innerReach;
                innerR[i] = pts[i] + innerShift - m * innerReach;
                outerL[i] = pts[i] + outerShift + m * outerReach;
                outerR[i] = pts[i] + outerShift - m * outerReach;
            }

            const Vector4 solid = Premultiply(col);
            for (std::size_t s = 0; s < segments; s++) {
                const std::size_t i = s, j = (s + 1) % n;
                if (innerReach > 0.0F) {
                    renderer.DrawShadedTriangle(innerL[i], innerL[j], innerR[j], solid, solid,
                                                solid);
                    renderer.DrawShadedTriangle(innerL[i], innerR[j], innerR[i], solid, solid,
                                                solid);
                }
                FadeQuad(renderer, innerL[i], innerL[j], outerL[i], outerL[j], solid);
                FadeQuad(renderer, innerR[i], innerR[j], outerR[i], outerR[j], solid);
            }
            if (!closed) {
                FadeQuad(renderer, innerL[0], innerR[0], outerL[0], outerR[0], solid);
                FadeQuad(renderer, innerL[n - 1], innerR[n - 1], outerL[n - 1], outerR[n - 1],
                         solid);
            }
        }

        void OverlayFillCircle(client::IRenderer& renderer, const Vector2& center, float radius,
                               const Vector4& c) {
            if (radius <= 0.0F)
                return;
            const std::size_t count = CircleSegments(radius);
            PointScratch pts(count);
            CirclePoints(center, radius, count, pts);
            OverlayFillConvexPolygon(renderer, pts.Data(), count, c);
        }

        void OverlayStrokeCircle(client::IRenderer& renderer, const Vector2& center,
                                 float radius, float width, const Vector4& c) {
            if (width <= 0.0F)
                return;
            const float outer = radius + width * 0.5F;
            if (radius <= width * 0.5F) {
                OverlayFillCircle(renderer, center, outer, c); // the ring's hole has closed
                return;
            }
            const std::size_t count = CircleSegments(outer);
            PointScratch pts(count);
            CirclePoints(center, radius, count, pts);
            OverlayStrokePolyline(renderer, pts.Data(), count, width, c, true);
        }

        bool OverlayInRect(const Vector2& p, float x, float y, float w, float h) {
            return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
        }
    } // namespace gui
} // namespace spades
