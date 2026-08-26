/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

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

#include <algorithm>

#include "Timer.h"
#include "UIManager.h"

namespace spades {
	namespace gui {
		namespace ui {
			void KeyRepeatManager::KeyDown(const std::string& key) {
				lastKey = key;
				nextDelay = 0.2F;
			}

			void KeyRepeatManager::KeyUp() { lastKey.clear(); }

			void KeyRepeatManager::RunFrame(float dt) {
				if (lastKey.empty())
					return;
				nextDelay -= dt;
				if (nextDelay < 0.0F) {
					if (handler)
						handler(lastKey);
					nextDelay = std::max(nextDelay + 0.06F, 0.0F);
				}
			}

			Timer::Timer(UIManager* manager) : manager(manager) {}

			Timer::~Timer() {}

			void Timer::OnTick() {
				if (tick)
					tick(*this);
			}

			void Timer::RunFrame(float dt) {
				// `UIManager` ticks a snapshot of its timer list, so this can be reached
				// after another timer's handler stopped us — including by destroying the
				// object whose `this` our `tick` captured. Never fire once stopped.
				if (!running)
					return;

				nextDelay -= dt;
				if (nextDelay < 0.0F) {
					OnTick();
					if (!running) // the handler stopped us
						return;

					if (autoReset)
						nextDelay = std::max(nextDelay + interval, 0.0F);
					else
						Stop();
				}
			}

			void Timer::Start() {
				nextDelay = interval;
				if (running)
					return; // already registered; the countdown above is the restart

				running = true;
				manager->AddTimer(this);
			}

			void Timer::Stop() {
				if (!running)
					return;

				running = false;
				manager->RemoveTimer(this);
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
