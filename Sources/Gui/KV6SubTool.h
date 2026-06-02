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

		// A sub-tool of a main tool (e.g. Select's Point / Rect). Shown as a button
		// in the secondary toolbar.
		class SubTool {
		public:
			virtual ~SubTool() {}
			virtual const char* Label() const = 0;
			virtual void OnActivate(KV6EditorView&) {}
			virtual void OnPointerDown(KV6EditorView&, const std::string& button) {}
			virtual void OnPointerUp(KV6EditorView&, const std::string& button) {}
			virtual void OnKey(KV6EditorView&, const std::string& key, bool down) {}
			// Abort an in-progress operation (Esc); return true if consumed.
			virtual bool OnEscape(KV6EditorView&) { return false; }
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

		// A 3-point axis-aligned box: corner, opposite corner (on the clicked face's
		// plane), then depth. The corner/depth are placed in free space, so the box
		// can be sized beyond the existing model. The action applied to the cells
		// (fill voxels, or add to the selection) is injected, so Draw and Select
		// reuse the same code.
		class RectSubTool : public SubTool {
		public:
			using ApplyFn = std::function<void(KV6EditorView&, const std::vector<IntVector3>&)>;

			// `apply` runs when the final click is LMB, `applyAlt` when it is RMB
			// (e.g. fill vs cut, or select vs deselect).
			RectSubTool(const char* label, ApplyFn apply, ApplyFn applyAlt, bool useMirror = false)
			    : label(label), apply(std::move(apply)), applyAlt(std::move(applyAlt)),
			      useMirror(useMirror) {}

			const char* Label() const override { return label; }
			void OnActivate(KV6EditorView&) override;
			void OnPointerDown(KV6EditorView&, const std::string& button) override;
			bool OnEscape(KV6EditorView&) override;
			void DrawScene(KV6EditorView&) override;

		private:
			const char* label;
			ApplyFn apply;
			ApplyFn applyAlt;
			bool useMirror;

			int stage = 0;            // 0 none, 1 corner set, 2 rectangle set
			IntVector3 p1;            // first corner
			int normalAxis = 2;       // axis of the clicked face's normal
			IntVector3 rectLo, rectHi; // in-plane rectangle (after the 2nd click)

			void Reset() { stage = 0; }
			// Construction point for the current stage, placed in free space so the
			// box can be sized beyond existing voxels.
			bool ShapeCur(KV6EditorView& ed, IntVector3& out) const;
			void BBox(const IntVector3& cur, IntVector3& lo, IntVector3& hi) const;
			void Cells(const IntVector3& cur, std::vector<IntVector3>& out) const;
		};

		// Move the current selection by dragging a 3-axis gizmo at its centroid.
		class MoveSubTool : public SubTool {
		public:
			const char* Label() const override { return "Move"; }
			void OnActivate(KV6EditorView&) override;
			void OnPointerDown(KV6EditorView&, const std::string& button) override;
			void OnPointerUp(KV6EditorView&, const std::string& button) override;
			bool OnEscape(KV6EditorView&) override;
			void DrawScene(KV6EditorView&) override;

		private:
			int grabAxis = -1;   // 0/1/2 while dragging a handle, else -1
			Vector2 grabCursor;  // cursor at grab start
			int curOffset = 0;   // current preview offset along grabAxis
			int OffsetAlong(KV6EditorView& ed, const Vector3& c, int axis) const;
		};
	} // namespace gui
} // namespace spades
