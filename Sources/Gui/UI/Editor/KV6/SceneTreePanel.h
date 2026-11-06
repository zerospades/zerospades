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

#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Components/InlineTextField.h>
#include <Gui/UI/Framework/DoubleClick.h>
#include <Gui/UI/Widgets/TextEntry.h>

#include "KV6Scene.h"

namespace spades {
	namespace client {
		class IFont;
		class IRenderer;
	} // namespace client
	namespace gui {
		/**
		 * The scene's tree, down the left of the viewport in Object mode: the
		 * scene at its root, then every object under its parent. It has no
		 * background: its rows are drawn over the view, as CAD tools draw
		 * theirs, and a press where there is no row reaches the view.
		 *
		 * A click selects, Ctrl or Cmd adds or takes away a row, Shift selects
		 * the rows from the last one clicked; dragging the selection drops it
		 * into a row, or between two. The eye at a row's end hides it with all
		 * under it, or shows it again; the arrow at its start folds it. A
		 * double click on a name (or F2, or the menu's Rename) types a new one
		 * in its place.
		 *
		 * It works on the scene it is handed each call, keeping only how it is
		 * shown and the press it is in the middle of.
		 */
		class SceneTreePanel {
		public:
			SceneTreePanel(client::IRenderer& renderer, client::IFont& font);

			/** The panel starts at (left, top) and stops above `bottom`. */
			void Layout(float left, float top, float bottom);
			/** The root row's label: the scene's file. */
			void SetSceneName(const std::string& name) { sceneName = name; }
			/** Back to the top, nothing held: another scene is shown. */
			void Reset();

			/** Whether `at` is on a row, where presses are the panel's. */
			bool IsOver(const KV6Scene& scene, const Vector2& at) const;
			/**
			 * A left press on a row (IsOver), or on the name being typed
			 * (IsOverRename): `toggle` for Ctrl or Cmd held, `extend` for Shift,
			 * at `time` in seconds, which tells a double click. Its moves and its
			 * release are the panel's until it ends (IsHolding). Returns what the
			 * user is told, if anything.
			 */
			std::string Press(KV6Scene& scene, const Vector2& at, bool toggle, bool extend,
			                  float time);
			bool IsHolding() const { return held != Held::None; }
			void Move(const KV6Scene& scene, const Vector2& at);
			/** Ends the press: a click selects, a drag drops. Returns what the
			 *  user is told: why the drop was refused, or nothing. */
			std::string Release(KV6Scene& scene, const Vector2& at);
			/**
			 * A right press: the row under `at` (empty for the scene's) in
			 * `row`, selected alone first unless it is selected already, as the
			 * menu it opens acts on the selection. False off the rows.
			 */
			bool ContextRow(KV6Scene& scene, const Vector2& at, NodePath& row);
			/** Abandons a drag; false when there was none. */
			bool CancelDrag();
			/** The wheel over the rows scrolls them; false elsewhere. */
			bool Wheel(const KV6Scene& scene, const Vector2& at, float amount);
			/** Unfolds what is above the node and scrolls it into sight. */
			void Reveal(KV6Scene& scene, const NodePath& path);

			/** `time` in seconds, which blinks the caret. */
			void Draw(const KV6Scene& scene, const Vector2& cursor, float time);

			// --- Renaming: a new name typed over the old one, in its row -------
			/** Starts on the node at `path`, all of its name selected. False for
			 *  the scene's row, or a node that keeps its name (`why` says so). */
			bool BeginRename(KV6Scene& scene, const NodePath& path, float time, std::string& why);
			bool IsRenaming() const { return nameField.IsOpen(); }
			/** Whether `at` is on the name being typed, where presses place the
			 *  caret and drags select. */
			bool IsOverRename(const KV6Scene& scene, const Vector2& at) const;
			/**
			 * Gives the node the name as typed, its ends trimmed. True when that
			 * ended the typing (renamed, or the name unchanged), `said` telling
			 * the user which; false when refused, `said` then saying why, and the
			 * name stays open to be put right.
			 */
			bool CommitRename(KV6Scene& scene, std::string& said);
			void CancelRename();
			/**
			 * A key down while renaming: Enter (or Tab) settles it, Escape gives
			 * it up, the editing keys edit; every other key is taken, so nothing
			 * else answers to a letter meant for the name. Returns what the user
			 * is told, if anything.
			 */
			std::string RenameKey(KV6Scene& scene, const std::string& key,
			                      const TextEntry::Modifiers& modifiers);
			/** What was typed, and what an input method has yet to settle. */
			void RenameText(const std::string& text);
			void RenameComposition(const std::string& text);
			/** Where the name is typed, for an input method's window. */
			AABB2 RenameRect(const KV6Scene& scene) const;

		private:
			struct Row {
				NodePath path;          // empty for the scene
				const VoxelObject* node; // null for the scene
				int depth;
				bool hasChildren;
				bool folded;
				bool hiddenAbove; // a node above it is hidden
				bool last;        // the last of its parent's children
				// For each node above it but the scene, from the top: whether
				// children of its parent come after it, so its branch goes on
				// past this row.
				std::vector<bool> branchesOn;
			};
			enum class Part { Label, Fold, Eye };
			enum class Held { None, Row, Drag, Text, Swallowed };
			// Where a drag would land, and how it is shown.
			struct Drop {
				bool any = false; // over the panel at all
				NodePath parent;
				int position = -1;
				bool into = false; // into a row (else between two, as a line)
				int row = -1;      // the row it goes into, or the line's row
				float lineY = 0.0F, lineX = 0.0F;
				std::string why; // empty when allowed
			};

			client::IRenderer& renderer;
			client::IFont& font;
			float left = 0.0F, top = 0.0F, bottom = 0.0F;
			std::string sceneName = "Scene";
			int scroll = 0; // rows scrolled past

			Held held = Held::None;
			NodePath pressed;
			Vector2 pressAt = MakeVector2(0.0F, 0.0F);
			bool selectOnRelease = false; // a click on a selected row selects it alone
			long pressShape = 0;          // the tree's shape the press began on
			std::vector<NodePath> dragged;
			NodePath anchor; // where Shift ranges start
			long anchorShape = -1;
			// Clicks on names, a second soon after on the same one renaming it.
			ui::DoubleClick rowClicks;

			// The name being typed, over the node's label.
			NodePath renamed;
			long renameShape = 0;
			InlineTextField nameField;

			std::vector<Row> Rows(const KV6Scene& scene) const;
			int VisibleRows() const;
			int ClampedScroll(int rowCount) const;
			float RowTop(int row) const;
			/** The panel behind the rows shown, of `rowCount` in all. */
			AABB2 Backdrop(int rowCount) const;
			float LabelX(int depth) const;
			/** The x the branch to the children at `depth` comes down at: under
			 *  their parent's fold arrow. */
			float BranchX(int depth) const;
			/** The row under `at` among those drawn, or -1. */
			int RowAt(const std::vector<Row>& rows, const Vector2& at) const;
			Part PartAt(const Row& row, const Vector2& at) const;
			Drop DropAt(const KV6Scene& scene, const std::vector<Row>& rows, const Vector2& at) const;
			std::string Label(const Row& row) const;
			/** The row showing `path`, or -1 (folded away, or gone). */
			int RowOf(const std::vector<Row>& rows, const NodePath& path) const;
			/** The box a name is typed into on `row`, shown at `index`. */
			AABB2 RenameBox(const Row& row, int index) const;
			/** Puts the name's box where its row is now. */
			void PlaceRename(const KV6Scene& scene);
			/** Why the name as typed would be refused, under its box. */
			void DrawRenameReason(const KV6Scene& scene);
			std::string Elide(const std::string& text, float width) const;

			void DrawBranches(const Row& row, float y);
			void DrawText(const std::string& text, float x, float y, const Vector4& color);
			void DrawEye(const Vector2& centre, bool open, const Vector4& color);
			void DrawLock(const Vector2& centre, const Vector4& color);
			void DrawFold(const Vector2& centre, bool folded, const Vector4& color);
		};
	} // namespace gui
} // namespace spades
