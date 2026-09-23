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
	/**
	 * Tells the desktop that this program opens voxel model files, so a file
	 * manager offers it for one.
	 *
	 * Only Windows needs this at runtime: there, an association is a registry
	 * entry a program makes for itself. A package installs the equivalent on
	 * Linux (a MIME definition) and a bundle carries it on macOS (the document
	 * types in its Info.plist), so both report that there is nothing to do.
	 *
	 * The registration is per-user and additive: it adds this program to the
	 * list of programs offered for a model without taking the file type from
	 * whatever already holds it, so it needs no administrator and displaces
	 * nothing. `Unregister` removes exactly what `Register` wrote.
	 *
	 * Both return an empty string on success, or a message saying what failed.
	 */
	std::string RegisterModelFileTypes();
	std::string UnregisterModelFileTypes();
} // namespace spades
