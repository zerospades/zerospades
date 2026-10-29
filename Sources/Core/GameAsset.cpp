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

#include <cctype>

#include "GameAsset.h"

namespace spades {
	namespace {
		bool StartsWith(const std::string& s, const char* prefix) {
			return s.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
		}

		bool EndsWith(const std::string& s, const char* suffix) {
			std::size_t n = std::char_traits<char>::length(suffix);
			return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
		}
	} // namespace

	std::string NormalizeResourcePath(const std::string& path) {
		std::string out = path;
		for (char& c : out) {
			if (c == '\\')
				c = '/';
			else
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		// FileManager serves `foo.weak` as `foo` when nothing has `foo` itself.
		if (EndsWith(out, ".weak"))
			out.resize(out.size() - 5);
		return out;
	}

	std::string GetAssetRequestPath(const std::string& file) {
		std::string path = file;
		std::string lower = path;
		for (char& c : lower)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (EndsWith(lower, ".weak")) {
			path.resize(path.size() - 5);
			lower.resize(lower.size() - 5);
		}
		// ".meta.json" counts as one extension.
		std::size_t ext = EndsWith(lower, ".meta.json") ? path.size() - 10 : path.rfind('.');
		std::size_t slash = path.find_last_of("/\\");
		if (ext != std::string::npos && (slash == std::string::npos || ext > slash)) {
			for (std::size_t i = ext; i < path.size(); i++)
				path[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(path[i])));
		}
		return path;
	}

	bool IsUnreadModFile(const std::string& path) {
		std::string p = NormalizeResourcePath(path);
		std::size_t slash = p.rfind('/');
		std::string name = slash == std::string::npos ? p : p.substr(slash + 1);
		return (!name.empty() && name[0] == '.') || name == "thumbs.db" ||
		       name == "desktop.ini" || EndsWith(name, ".txt") || EndsWith(name, ".md");
	}

	std::string GetModelPathOfMetadata(const std::string& metadata) {
		std::string path = GetAssetRequestPath(metadata);
		return path.substr(0, path.size() - 10) + ".kv6";
	}

	GameAssetKind GetGameAssetKind(const std::string& path) {
		std::string p = NormalizeResourcePath(path);

		if (StartsWith(p, "scripts/"))
			return GameAssetKind::Script;
		if (EndsWith(p, ".kv6"))
			return GameAssetKind::Model;
		if (EndsWith(p, ".meta.json"))
			return GameAssetKind::ModelMetadata;
		if (EndsWith(p, ".png") || EndsWith(p, ".jpg") || EndsWith(p, ".tif") ||
		    EndsWith(p, ".bmp") || EndsWith(p, ".tga"))
			return GameAssetKind::Image;
		if (EndsWith(p, ".opus") || EndsWith(p, ".ogg") || EndsWith(p, ".wav"))
			return GameAssetKind::Sound;
		return GameAssetKind::None;
	}
} // namespace spades
