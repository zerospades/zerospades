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

#include "ScriptFunction.h"
#include <Core/Math.h>

namespace spades {
	namespace client {
		class ScriptIViewToolSkin {
			asIScriptObject* obj;

		public:
			ScriptIViewToolSkin(asIScriptObject* obj);
			void SetEyeMatrix(Matrix4 m);
			void SetSwing(Vector3);
			Vector3 GetLeftHandPosition();
			Vector3 GetRightHandPosition();
			void Draw2D();
		};

		/** Optional, for a view skin that handles left-handedness itself rather
		 * than having the client mirror it: the client then tells it which hand
		 * the player holds the tool in, never flips its sway nor swaps or
		 * mirrors the arms (the hand positions are the player's own left and
		 * right), and takes the direction brass flies out in from it. A skin
		 * that does not implement it is mirrored whole when left-handed, as
		 * always. */
		class ScriptIViewToolSkin2 {
			asIScriptObject* obj;

		public:
			ScriptIViewToolSkin2(asIScriptObject* obj);
			bool ImplementsInterface();
			void SetLeftHanded(bool);
			/** The direction brass flies out in, in the frame of the skin's own
			 *  positions (as MuzzlePosition); zero for the client's own. */
			Vector3 GetCaseEjectDirection();
		};
	} // namespace client
} // namespace spades