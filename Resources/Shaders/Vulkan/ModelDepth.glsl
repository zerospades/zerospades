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

// The depth pre-pass's fragment stage for models: nothing but what decides
// whether a fragment is there at all, so that the depth it leaves is the depth
// the lit pass meets. The lit pass clips away what lies below the water plane in
// the mirror's view (`waterClip`, +inf-based in the scene's), and so does this.
// Includers define WATER_CLIP_LOCATION, where their vertex shader puts it.

layout(location = WATER_CLIP_LOCATION) in float waterClip;

void main() {
	if (waterClip < 0.0)
		discard;
}
