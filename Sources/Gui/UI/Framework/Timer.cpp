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
				nextDelay -= dt;
				if (nextDelay < 0.0F) {
					OnTick();
					if (autoReset)
						nextDelay = std::max(nextDelay + interval, 0.0F);
					else
						Stop();
				}
			}

			void Timer::Start() {
				nextDelay = interval;
				manager->AddTimer(this);
			}

			void Timer::Stop() { manager->RemoveTimer(this); }
		} // namespace ui
	} // namespace gui
} // namespace spades
