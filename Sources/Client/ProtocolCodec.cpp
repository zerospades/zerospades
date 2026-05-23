/*
 Copyright (c) 2013 yvt
 based on code of pysnip (c) Mathias Kaerlev 2011-2012.

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
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "ProtocolCodec.h"

#include <Core/CP437.h>
#include <Core/Settings.h>

DEFINE_SPADES_SETTING(cg_unicode, "1");

namespace spades {
	namespace client {

		namespace {
			const char UTFSign = -1;
		} // namespace

		std::string EncodeString(std::string str) {
			auto str2 = CP437::Encode(str, -1);
			if (!cg_unicode)
				return str2; // ignore fallbacks

			// some fallbacks; always use UTF8
			if (str2.find(-1) != std::string::npos)
				str.insert(0, &UTFSign, 1);
			else
				str = str2;

			return str;
		}

		std::string DecodeString(std::string s) {
			if (s.size() > 0 && s[0] == UTFSign)
				return s.substr(1);

			return CP437::Decode(s);
		}

		PlayerInput ParsePlayerInput(uint8_t bits) {
			PlayerInput inp;
			inp.moveForward = (bits & (1 << 0)) != 0;
			inp.moveBackward = (bits & (1 << 1)) != 0;
			inp.moveLeft = (bits & (1 << 2)) != 0;
			inp.moveRight = (bits & (1 << 3)) != 0;
			inp.jump = (bits & (1 << 4)) != 0;
			inp.crouch = (bits & (1 << 5)) != 0;
			inp.sneak = (bits & (1 << 6)) != 0;
			inp.sprint = (bits & (1 << 7)) != 0;
			return inp;
		}
		WeaponInput ParseWeaponInput(uint8_t bits) {
			WeaponInput inp;
			inp.primary = ((bits & (1 << 0)) != 0);
			inp.secondary = ((bits & (1 << 1)) != 0);
			return inp;
		}

	} // namespace client
} // namespace spades
