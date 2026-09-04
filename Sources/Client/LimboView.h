/*
 Copyright (c) 2013 yvt

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

#include "GameConstants.h"

namespace spades {
	namespace client {

		/**
		 * The team/weapon choice the limbo menu edits and `SpawnPressed` reads.
		 * Layout, input and drawing live in `gui::LimboMenu`; this is state only.
		 */
		class LimboView {
			int selectedTeam;
			WeaponType selectedWeapon;

		public:
			LimboView();
			~LimboView();

			int GetSelectedTeam() { return selectedTeam; }
			WeaponType GetSelectedWeapon() { return selectedWeapon; }
			/** The protocol spells spectator 255, but everything here expects 2. */
			void SetSelectedTeam(int team) { selectedTeam = (team > 2) ? 2 : team; }
			void SetSelectedWeapon(WeaponType type) { selectedWeapon = type; }
		};
	} // namespace client
} // namespace spades