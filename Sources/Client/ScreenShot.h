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

namespace spades {
	namespace client {
		class IRenderer;

		// Reads the just-presented frame from the renderer and writes it to
		// `Screenshots/shotNNNN.<ext>`, where the extension follows the
		// `cg_screenshotFormat` setting. Returns the saved path; throws on failure.
		// Shared by the in-game client and the in-app editor.
		std::string SaveScreenShot(IRenderer& renderer);
	} // namespace client
} // namespace spades
