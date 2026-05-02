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

#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <Core/Math.h>

namespace spades {
	namespace gui {
		class KV6EditorView;

		// A sub-tool of a main tool (e.g. Select's Point / Rect / Cylinder).
		// Shown as a button in the secondary toolbar.
		class SubTool {
		public:
			virtual ~SubTool() {}
			virtual const char* Label() const = 0;
			virtual void OnActivate(KV6EditorView&) {}
			virtual void OnPointerDown(KV6EditorView&, const std::string& button) {}
			virtual void OnKey(KV6EditorView&, const std::string& key, bool down) {}
			virtual void DrawScene(KV6EditorView&) {}
		};

		// Single-voxel placement / deletion (Draw's "Block"): LMB places (or samples
		// a colour with Alt / pick mode), RMB deletes.
		class BlockSubTool : public SubTool {
		public:
			const char* Label() const override { return "Block"; }
			void OnActivate(KV6EditorView&) override;
			void OnPointerDown(KV6EditorView&, const std::string& button) override;
			void DrawScene(KV6EditorView&) override;
		};

		// Single-voxel selection toggle (Select's "Point").
		class PointSubTool : public SubTool {
		public:
			const char* Label() const override { return "Point"; }
			void OnActivate(KV6EditorView&) override;
			void OnPointerDown(KV6EditorView&, const std::string& button) override;
			void DrawScene(KV6EditorView&) override;
		};

		// Flood-fill selection by colour (Select's "By Colour"); also bound to [L].
		class ByColourSubTool : public SubTool {
		public:
			const char* Label() const override { return "By Colour"; }
			void OnActivate(KV6EditorView&) override;
			void OnPointerDown(KV6EditorView&, const std::string& button) override;
			void OnKey(KV6EditorView&, const std::string& key, bool down) override;
			void DrawScene(KV6EditorView&) override;
		};

		// A 3-point rectangle/cylinder region. The geometry is shared; the action
		// applied to the resulting cells (fill voxels, or add to the selection) is
		// injected, so Draw and Select reuse the same code.
		class ShapeSubTool : public SubTool {
		public:
			enum Kind { Rect, Cylinder };
			using ApplyFn = std::function<void(KV6EditorView&, const std::vector<IntVector3>&)>;

			ShapeSubTool(Kind kind, const char* label, ApplyFn apply)
			    : kind(kind), label(label), apply(std::move(apply)) {}

			const char* Label() const override { return label; }
			void OnActivate(KV6EditorView&) override;
			void OnPointerDown(KV6EditorView&, const std::string& button) override;
			void DrawScene(KV6EditorView&) override;

		private:
			Kind kind;
			const char* label;
			ApplyFn apply;

			int stage = 0;            // 0 none, 1 corner/centre, 2 rect/radius set
			IntVector3 p1;            // first corner / centre
			int normalAxis = 2;       // axis of the clicked face's normal
			IntVector3 rectLo, rectHi; // in-plane rectangle (Rect, after 2nd click)
			int radius = 0;           // (Cylinder, after 2nd click)

			void Reset() { stage = 0; }
			int RadiusTo(const IntVector3& cur) const;
			void BBox(const IntVector3& cur, IntVector3& lo, IntVector3& hi) const;
			void Cells(const IntVector3& cur, std::vector<IntVector3>& out) const;
		};
	} // namespace gui
} // namespace spades
