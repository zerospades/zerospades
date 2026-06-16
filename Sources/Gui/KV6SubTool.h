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

#include "KV6ClickSequence.h"
#include "KV6EditorTool.h"
#include "KV6ToolEvent.h"

namespace spades {
	namespace gui {
		class IEditorContext;

		// Leaf tools shown as buttons in the secondary toolbar (e.g. Select's Point
		// / Rect). They are ordinary `EditorTool`s with no children of their own; a
		// `ContainerTool` (Draw, Select) groups them and forwards input to the active
		// one.

		// Single-voxel placement / deletion (Draw's "Block"): LMB places (or samples
		// a colour with Alt / pick mode), RMB deletes.
		class BlockSubTool : public EditorTool {
		public:
			const char* Label() const override { return "Block"; }
			void OnActivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void DrawScene(IEditorContext&) override;
		};

		// Single-voxel recolour (Paint's "Block"): LMB recolours the hovered voxel
		// and keeps painting while dragged; RMB or Alt+LMB samples a colour.
		class PaintBlockSubTool : public EditorTool {
		public:
			const char* Label() const override { return "Block"; }
			void OnActivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void DrawScene(IEditorContext&) override;
		};

		// Single-voxel selection toggle (Select's "Point").
		class PointSubTool : public EditorTool {
		public:
			const char* Label() const override { return "Point"; }
			void OnActivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void DrawScene(IEditorContext&) override;
		};

		// Flood-fill selection by colour (Select's "By Colour"); also bound to [L].
		class ByColourSubTool : public EditorTool {
		public:
			const char* Label() const override { return "By Colour"; }
			void OnActivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void OnKey(IEditorContext&, const KeyInput&) override;
			void DrawScene(IEditorContext&) override;
		};

		// A 3-point axis-aligned box: corner, opposite corner (on the clicked face's
		// plane), then depth. The corner/depth are placed in free space, so the box
		// can be sized beyond the existing model. The three clicks are tracked by a
		// `ClickSequence`; the action applied to the cells (fill voxels, or add to
		// the selection) is injected, so Draw and Select reuse the same code.
		class RectSubTool : public EditorTool {
		public:
			using ApplyFn = std::function<void(IEditorContext&, const std::vector<IntVector3>&)>;

			// `apply` runs when the final click is LMB, `applyAlt` when it is RMB
			// (e.g. fill vs cut, or select vs deselect).
			RectSubTool(const char* label, ApplyFn apply, ApplyFn applyAlt, bool useMirror = false,
			            const char* applyMsg = "Rect applied", const char* altMsg = "Rect cut")
			    : label(label), apply(std::move(apply)), applyAlt(std::move(applyAlt)),
			      useMirror(useMirror), applyMsg(applyMsg), altMsg(altMsg) {}

			const char* Label() const override { return label; }
			void OnActivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			bool OnEscape(IEditorContext&) override;
			void DrawScene(IEditorContext&) override;

		private:
			const char* label;
			ApplyFn apply;
			ApplyFn applyAlt;
			bool useMirror;
			const char* applyMsg; // status shown after an LMB apply
			const char* altMsg;   // status shown after an RMB (alt) apply

			ClickSequence seq;  // the corner / opposite-corner / depth clicks
			int normalAxis = 2; // axis of the clicked face's normal (set on click 1)

			// Construction point for the current stage (seq.Count() == 1 -> opposite
			// corner on the face plane; == 2 -> depth along the normal), placed in
			// free space so the box can be sized beyond existing voxels.
			bool StagePoint(IEditorContext& ed, IntVector3& out) const;
			// Inclusive box spanned by the recorded points plus an in-progress one
			// (`pts` holds 2 or 3 points: corner, opposite corner, [depth]).
			void BBoxOf(const std::vector<IntVector3>& pts, IntVector3& lo, IntVector3& hi) const;
			void CellsOf(const std::vector<IntVector3>& pts, std::vector<IntVector3>& out) const;
		};

		// Move the current selection by dragging a 3-axis gizmo at its centroid.
		class MoveSubTool : public EditorTool {
		public:
			const char* Label() const override { return "Move"; }
			void OnActivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			bool OnEscape(IEditorContext&) override;
			void DrawScene(IEditorContext&) override;
			void DrawOverlay(IEditorContext&) override;

		private:
			int grabAxis = -1;   // 0/1/2 while dragging a handle, else -1
			Vector2 grabCursor;  // cursor at grab start
			int curOffset = 0;   // current preview offset along grabAxis
			int OffsetAlong(IEditorContext& ed, const Vector3& c, int axis) const;
			// Axis whose handle the cursor is over (line or tip cube), else -1.
			int HitAxis(IEditorContext& ed, const Vector3& c) const;
		};
	} // namespace gui
} // namespace spades
