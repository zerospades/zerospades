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

namespace spades {
	namespace client {
		class IFont;
		class IRenderer;
	} // namespace client
	namespace gui {
		/**
		 * A slider over a row of steps, each a choice of its own (1/10 ... 10/1):
		 * the handle stops on one of them. Dragged, clicked along the track, or
		 * stepped with the keys; a step can be left unusable, which the handle
		 * passes by, drawn faint. Major steps are labelled under the track, and
		 * the chosen one over the handle.
		 *
		 * It knows nothing of what the steps mean: its owner reads `Index()`
		 * after a call says it changed. It draws in the editor's theme.
		 */
		class StepSlider {
		public:
			struct Step {
				std::string label;
				bool major = false;  // labelled under the track
				bool usable = true;  // false: drawn faint, never chosen
			};

			StepSlider(client::IRenderer& renderer, client::IFont& font);

			/** The steps, the one at `index` chosen (or the nearest usable). */
			void SetSteps(std::vector<Step> steps, int index);
			/** Whether step `index` can be chosen; the choice moves off one
			 *  that no longer can. */
			void SetUsable(int index, bool usable);
			int Index() const { return index; }
			int Count() const { return int(steps.size()); }

			/** Its box: the track runs across it, the labels below. */
			void Layout(float x, float y, float width);
			static float Height();
			bool Contains(const Vector2& at) const;

			/** A left press on it: holds the handle and moves it there. True
			 *  when the choice changed. */
			bool Press(const Vector2& at);
			/** The pointer while held; true when the choice changed. */
			bool Drag(const Vector2& at);
			void Release() { held = false; }
			bool IsHeld() const { return held; }
			/** Left, Right, Home, End: true when the choice changed, false
			 *  also for keys it has no use for. */
			bool Key(const std::string& key);

			void Draw(const Vector2& cursor);

		private:
			client::IRenderer& renderer;
			client::IFont& font;
			std::vector<Step> steps;
			int index = 0;
			bool held = false;
			float x = 0.0F, y = 0.0F, width = 0.0F;

			float StepX(int i) const;
			/** The usable step nearest `at` along the track; -1 for none. */
			int NearestAt(float at) const;
			/** The usable step nearest `i`, `i` itself when it is one. */
			int NearestUsable(int i) const;
			bool Choose(int i);
		};
	} // namespace gui
} // namespace spades
