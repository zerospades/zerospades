/*
 Copyright (c) 2026 ZeroSpades developers.

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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <string>

namespace spades {
	/**
	 * The kinds of game assets: what a game loads through its GameResources, and
	 * what a skin can bring. Every other file (fonts, shaders, translations...)
	 * belongs to the software.
	 */
	enum class GameAssetKind {
		None,
		Model,         // .kv6
		ModelMetadata, // the .meta.json VoxelModelLoader reads beside a model
		Image,         // a format Bitmap::Load reads
		Sound,
		Script, // the script module only holds the skins
	};

	GameAssetKind GetGameAssetKind(const std::string& path);

	/**
	 * The key two spellings of one resource path share, the way zip entries
	 * compare: case-insensitive, with '/' as the separator; `foo.weak` is `foo`.
	 */
	std::string NormalizeResourcePath(const std::string& path);

	/**
	 * The name the game asks for a file in a pak by: without `.weak`, and with
	 * the extension in lower case, which some loaders pick a codec by.
	 */
	std::string GetAssetRequestPath(const std::string& file);

	/**
	 * Whether nothing in the game reads `path`: a readme, a licence text, or what
	 * a file manager leaves behind (`.DS_Store`, `Thumbs.db`, `desktop.ini`).
	 */
	bool IsUnreadModFile(const std::string& path);

	/** The model a `.meta.json` file goes with: `Foo.meta.json` is `Foo.kv6`. */
	std::string GetModelPathOfMetadata(const std::string& metadata);
} // namespace spades
