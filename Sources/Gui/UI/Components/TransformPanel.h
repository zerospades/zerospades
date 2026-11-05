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

#include <memory>
#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Widgets/TextEntry.h>

namespace spades {
	namespace client {
		class IFont;
		class IRenderer;
	} // namespace client
	namespace gui {
		class NumberField;

		/**
		 * What a TransformPanel shows and sets: something placed by a
		 * position, a rotation (turns about X, then Y, then Z, in degrees) and
		 * a scale. Setting one goes as a preview the target shows live, then
		 * is kept (as one step of its history) or given up.
		 */
		class ITransformTarget {
		public:
			struct Transform {
				Vector3 position;
				Vector3 rotation; // degrees
				Vector3 scale;
			};
			/** What it has to set, which the panel shows rows for. */
			enum Rows : unsigned { kLocation = 1, kRotation = 2, kScale = 4, kAllRows = 7 };
			virtual ~ITransformTarget() {}

			virtual unsigned ShownRows() const { return kAllRows; }
			/** Whether its scale's lock is offered (IsScaleLocked), on the
			 *  panel's Scale row. */
			virtual bool CanLockScale() const { return false; }
			/** The panel's title: what is set ("Transform", "Pivot"). */
			virtual std::string Title() const { return "Transform"; }

			/** What there is to show: false when nothing. `name` titles it;
			 *  `onlyMoves` when its rotation and scale cannot be set. */
			virtual bool CurrentTransform(Transform& transform, std::string& name,
			                              bool& onlyMoves) const = 0;
			virtual void PreviewTransform(const Transform& transform) = 0;
			virtual void CommitTransform() = 0;
			virtual void CancelTransform() = 0;
		};

		/**
		 * Whether scaling keeps proportions, wherever the lock is offered: one
		 * axis set in the panel sets the others by the same factor, and a
		 * gizmo scales evenly only. One setting for the editor, kept across
		 * sessions.
		 */
		bool IsScaleLocked();
		void SetScaleLocked(bool locked);

		/**
		 * Location, Rotation and Scale as three numbers each, as professional
		 * tools show the active object's (Blender's sidebar, Unity's
		 * Inspector): each is scrubbed by dragging it sideways or typed, and
		 * every change is a preview while it goes on and one step once kept.
		 *
		 * It works on the target it is handed each call. Its host routes it
		 * presses on it and the pointer's moves while one is held (without
		 * moving the pointer, so a scrub never runs into the screen's edge),
		 * and the keyboard while a number is typed.
		 */
		class TransformPanel {
		public:
			TransformPanel(client::IRenderer& renderer, client::IFont& font);
			~TransformPanel();

			/** Its top right corner. */
			void Layout(float right, float top);
			/** Its bottom right corner: it grows up from there, as tall as the
			 *  rows its target has. */
			void LayoutAbove(float right, float bottom);
			/** Whether it showed last frame, and `at` is on it. */
			bool Contains(const Vector2& at) const;

			bool IsHolding() const;
			bool IsTyping() const;

			void Press(ITransformTarget& target, const Vector2& at);
			void Drag(ITransformTarget& target, float dx, bool fine, bool inSteps);
			void Release(ITransformTarget& target, float time);
			/** Gives up a scrub (Escape); false when none was going on. */
			bool CancelScrub(ITransformTarget& target);

			/** A key down while typing: Enter keeps, Tab goes on, Escape gives
			 *  up; every other key is the number's. */
			void Key(ITransformTarget& target, const std::string& key,
			         const TextEntry::Modifiers& modifiers, float time);
			void Insert(const std::string& text);
			void Compose(const std::string& text);
			AABB2 TextRect() const;
			/** Ends typing as a click elsewhere does. */
			void Settle(ITransformTarget& target);

			void Draw(ITransformTarget& target, const Vector2& cursor, float time);

		private:
			client::IRenderer& renderer;
			client::IFont& font;
			std::vector<std::unique_ptr<NumberField>> fields; // location, rotation, scale; x y z
			float right = 0.0F, top = 0.0F;
			bool fromBottom = false;
			float bottom = 0.0F;
			bool shown = false;
			unsigned rows = ITransformTarget::kAllRows; // as the target last said
			int RowCount() const;
			/** Where row `row` is drawn among those shown; -1 when it is not. */
			int Slot(int row) const;
			int active = -1; // the field being set
			ITransformTarget::Transform editing; // as it was when the field began, then as set
			Vector3 begunScale; // the scale when the field began, which a locked one keeps the
			                    // proportions of
			bool lockShown = false; // the Scale row's lock, as drawn last frame
			bool lockUsable = false;
			bool scaleLocked = false; // the lock, for the field being set
			/** Where the lock sits, at the end of the Scale row's label. */
			AABB2 LockRect() const;
			void DrawLock(const Vector2& cursor);

			float Left() const;
			float Height() const;
			void Place();
			/** Starts setting field `index` from the target as it is. */
			bool Begin(ITransformTarget& target, int index);
			/** The transform with field `index` at `value`. */
			void Put(int index, float value);
			void Act(ITransformTarget& target, int result, float time);
		};
	} // namespace gui
} // namespace spades
