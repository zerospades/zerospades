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
#include <Core/Math.h>

namespace spades {
	namespace gui {
		enum class MenuItemType {
			Team1,
			Team2,
			TeamSpectator,
			WeaponRifle,
			WeaponSMG,
			WeaponShotgun,
			Spawn,
			Close
		};

		struct MenuItem {
			MenuItemType type;
			AABB2 rect;
			std::string text;
			bool hover;
			bool visible;

			MenuItem() : type(MenuItemType::Team1), hover(false), visible(true) {}
			MenuItem(MenuItemType type, AABB2 rt, std::string txt)
			    : type(type), rect(rt), text(txt), hover(false), visible(true) {}
		};
	} // namespace gui
} // namespace spades
