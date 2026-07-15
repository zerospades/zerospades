/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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

#pragma once

#include <Core/RefCountedObject.h>

#include "Event.h"

namespace spades {
	namespace gui {
		namespace ui {
			class UIManager;

			/**
			 * Delivers a delayed key-repeat notification while a key is held down.
			 * Owned by value by `UIManager` (one for keys, one for characters).
			 */
			class KeyRepeatManager {
				std::string lastKey;
				float nextDelay = 0.0F;

			public:
				KeyRepeatEventHandler handler;

				void KeyDown(const std::string& key);
				void KeyUp();
				void RunFrame(float dt);
			};

			/** A repeating or one-shot timer driven by `UIManager`. */
			class Timer : public RefCountedObject {
				UIManager* manager; // weak; the manager outlives its timers
				float nextDelay = 0.0F;

			protected:
				~Timer();

			public:
				TimerTickEventHandler tick;

				/** Minimum interval with which the timer fires. */
				float interval = 1.0F;
				bool autoReset = true;

				Timer(UIManager* manager);

				UIManager& GetManager() const { return *manager; }

				void OnTick();

				/** Called by `UIManager`. */
				void RunFrame(float dt);

				void Start();
				void Stop();
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
