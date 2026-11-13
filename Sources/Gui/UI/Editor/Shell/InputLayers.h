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
#include <vector>

#include <Core/Math.h>

#include "ToolEvent.h"

namespace spades {
	namespace gui {
		/**
		 * An editor's input goes through layers: the things stacked over its
		 * view -- a dialog, an open menu, a panel, the bars -- and under them
		 * all the view itself, with its tool. A press or a key is offered to
		 * each in turn, topmost first, and belongs to the first that takes it;
		 * nothing below ever sees it.
		 *
		 * The order is the order they are added in, in one place, so what
		 * takes input before what reads off as a list, and something new to
		 * stack over the view is one more entry in it. A layer is named for
		 * that list's sake.
		 */

		/** A layer of the keyboard. */
		struct KeyLayer {
			const char* name;
			/** A key went down or up; true when the layer took it. */
			std::function<bool(const std::string& key, bool down)> key;
		};

		/** A layer of the pointer's buttons. */
		struct PointerLayer {
			const char* name;
			/** A button went down at `at`; true when the layer took the press.
			 *  One that only has something to do first, whoever takes it,
			 *  does it and says false. */
			std::function<bool(PointerButton button, const Vector2& at)> press;
			/**
			 * A button came up. Every layer is told, each letting go of what
			 * it holds and doing nothing if that is nothing, since the press
			 * it answers may have been let go of from elsewhere already. True
			 * ends it there. Null for a layer that holds nothing.
			 */
			std::function<bool(PointerButton button)> release;
		};

		/** Offers a key to `layers` in order; true when one took it. */
		inline bool OfferKey(const std::vector<KeyLayer>& layers, const std::string& key,
		                     bool down) {
			for (const KeyLayer& layer : layers) {
				if (layer.key(key, down))
					return true;
			}
			return false;
		}

		/** Offers a press to `layers` in order; true when one took it. */
		inline bool OfferPress(const std::vector<PointerLayer>& layers, PointerButton button,
		                       const Vector2& at) {
			for (const PointerLayer& layer : layers) {
				if (layer.press && layer.press(button, at))
					return true;
			}
			return false;
		}

		/** Tells `layers` of a release, in order, until one ends it. */
		inline void OfferRelease(const std::vector<PointerLayer>& layers, PointerButton button) {
			for (const PointerLayer& layer : layers) {
				if (layer.release && layer.release(button))
					return;
			}
		}
	} // namespace gui
} // namespace spades
