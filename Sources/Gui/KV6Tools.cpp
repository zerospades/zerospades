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

#include "KV6Tools.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <Core/Exception.h>
#include <Core/LocalFileSystem.h>
#include <Core/VoxelModel.h>
#include <Gui/UI/Editor/KV6/KV6ModelIO.h>

namespace spades {
	namespace {
		// Coordinates are voxel indices, 0 .. size - 1 on each axis, with Z
		// pointing down as in the game. Colours are RRGGBB in hex; the model
		// keeps them packed 0x00BBGGRR. The pivot is the point the model is
		// built about, as the editor shows it (the KV6 origin, negated).

		// A KV6 column is a 64-bit mask, so no model is deeper than this.
		constexpr int kMaxDepth = 64;
		constexpr int kMaxWidth = 4096;

		/** A command the user got wrong: said with the usage, exit code 2. */
		struct UsageError : std::runtime_error {
			using std::runtime_error::runtime_error;
		};

		const char* const kUsage =
		  "usage: --kv6-tools COMMAND FILE [ARGS...]\n"
		  "\n"
		  "Reads and edits KV6 voxel models without opening the game. Coordinates\n"
		  "are voxel indices from 0 (Z points down, as in the game); colours are\n"
		  "RRGGBB in hex (a leading # is allowed). Edits are saved in place.\n"
		  "\n"
		  "  new FILE W H D [--pivot X Y Z] [--force]\n"
		  "                         create an empty W x H x D model (D <= 64), its\n"
		  "                         pivot in the middle unless given\n"
		  "  info FILE [--json]     size, pivot, voxel count and bounds\n"
		  "  get-size FILE          W H D\n"
		  "  get-pivot FILE         X Y Z\n"
		  "  set-pivot FILE X Y Z   move the pivot (the voxels stay)\n"
		  "  get-rect FILE          MINX MINY MINZ MAXX MAXY MAXZ of the solid\n"
		  "                         voxels, or \"empty\"\n"
		  "  count-voxels FILE      how many voxels are solid\n"
		  "  get-voxel FILE X Y Z   RRGGBB, or \"air\"\n"
		  "  add-voxel FILE X Y Z COLOR\n"
		  "                         make a voxel solid in COLOR (replacing it)\n"
		  "  remove-voxel FILE X Y Z\n"
		  "  paint-voxel FILE X Y Z COLOR\n"
		  "                         recolour a solid voxel; air stays air\n"
		  "  fill-box FILE X0 Y0 Z0 X1 Y1 Z1 COLOR\n"
		  "                         every voxel of the box, corners included\n"
		  "  clear-box FILE X0 Y0 Z0 X1 Y1 Z1\n"
		  "  paint-box FILE X0 Y0 Z0 X1 Y1 Z1 COLOR\n"
		  "  list-voxels FILE       one solid voxel per line: X Y Z RRGGBB\n"
		  "  resize FILE W H D [DX DY DZ]\n"
		  "                         new size, voxels moved by DX DY DZ (default 0);\n"
		  "                         the pivot stays on the same voxels; what falls\n"
		  "                         outside is lost\n"
		  "  trim FILE              shrink the volume to the solid voxels\n"
		  "  apply FILE             run edits read from standard input, one per\n"
		  "                         line, then save once: any edit above without\n"
		  "                         its FILE (\"add-voxel 1 2 3 ff0000\"); blank\n"
		  "                         lines and lines starting with # are skipped\n"
		  "  help                   this text\n";

		int ParseInt(const std::string& text, const char* what) {
			char* end = nullptr;
			errno = 0;
			const long value = std::strtol(text.c_str(), &end, 10);
			if (text.empty() || *end != '\0' || errno != 0 || value < -1000000 || value > 1000000)
				throw UsageError(std::string("not a whole number for ") + what + ": '" + text + "'");
			return int(value);
		}

		float ParseFloat(const std::string& text, const char* what) {
			char* end = nullptr;
			const float value = std::strtof(text.c_str(), &end);
			if (text.empty() || *end != '\0')
				throw UsageError(std::string("not a number for ") + what + ": '" + text + "'");
			return value;
		}

		std::uint32_t ParseColor(std::string text) {
			if (!text.empty() && text[0] == '#')
				text = text.substr(1);
			if (text.size() == 8 && (text.compare(0, 2, "0x") == 0 || text.compare(0, 2, "0X") == 0))
				text = text.substr(2);
			char* end = nullptr;
			const unsigned long rgb = std::strtoul(text.c_str(), &end, 16);
			if (text.size() != 6 || *end != '\0')
				throw UsageError("not a colour (RRGGBB in hex): '" + text + "'");
			const std::uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
			return r | (g << 8) | (b << 16); // packed 0x00BBGGRR
		}

		std::string FormatColor(std::uint32_t packed) {
			char text[8];
			std::snprintf(text, sizeof(text), "%02x%02x%02x", packed & 0xFF, (packed >> 8) & 0xFF,
			              (packed >> 16) & 0xFF);
			return text;
		}

		std::string FormatFloat(float v) {
			char text[32];
			std::snprintf(text, sizeof(text), "%g", double(v));
			return text;
		}

		void CheckSize(int w, int h, int d) {
			if (w < 1 || h < 1 || d < 1 || w > kMaxWidth || h > kMaxWidth || d > kMaxDepth)
				throw UsageError("a size runs from 1x1x1 to " + std::to_string(kMaxWidth) + "x" +
				                 std::to_string(kMaxWidth) + "x" + std::to_string(kMaxDepth));
		}

		void CheckInside(const VoxelModel& m, int x, int y, int z) {
			if (x < 0 || y < 0 || z < 0 || x >= m.GetWidth() || y >= m.GetHeight() ||
			    z >= m.GetDepth())
				throw std::runtime_error(
				  "voxel " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z) +
				  " is outside the " + std::to_string(m.GetWidth()) + "x" +
				  std::to_string(m.GetHeight()) + "x" + std::to_string(m.GetDepth()) +
				  " model (resize it first)");
		}

		Handle<VoxelModel> Load(const std::string& path) {
			Handle<VoxelModel> model = gui::KV6ModelIO::Load(path);
			if (!model)
				throw std::runtime_error("cannot read a KV6 model from " + path);
			return model;
		}

		void Save(VoxelModel& model, const std::string& path) {
			if (!gui::KV6ModelIO::Save(model, path))
				throw std::runtime_error("cannot write " + path);
		}

		bool Bounds(const VoxelModel& m, int lo[3], int hi[3]) {
			bool any = false;
			for (int x = 0; x < m.GetWidth(); x++)
				for (int y = 0; y < m.GetHeight(); y++)
					for (int z = 0; z < m.GetDepth(); z++) {
						if (!m.IsSolid(x, y, z))
							continue;
						const int at[3] = {x, y, z};
						for (int k = 0; k < 3; k++) {
							lo[k] = any ? std::min(lo[k], at[k]) : at[k];
							hi[k] = any ? std::max(hi[k], at[k]) : at[k];
						}
						any = true;
					}
			return any;
		}

		int CountSolid(const VoxelModel& m) {
			int count = 0;
			for (int x = 0; x < m.GetWidth(); x++)
				for (int y = 0; y < m.GetHeight(); y++)
					for (int z = 0; z < m.GetDepth(); z++)
						count += m.IsSolid(x, y, z) ? 1 : 0;
			return count;
		}

		// A box given by two opposite corners, in either order, clipped to the
		// model; empty when it misses it altogether.
		struct Box {
			int lo[3], hi[3];
			bool Empty() const { return lo[0] > hi[0] || lo[1] > hi[1] || lo[2] > hi[2]; }
		};
		Box ParseBox(const VoxelModel& m, const std::vector<std::string>& a, std::size_t at) {
			Box box;
			const int size[3] = {m.GetWidth(), m.GetHeight(), m.GetDepth()};
			for (int k = 0; k < 3; k++) {
				const int c0 = ParseInt(a[at + k], "a corner");
				const int c1 = ParseInt(a[at + 3 + k], "a corner");
				box.lo[k] = std::max(0, std::min(c0, c1));
				box.hi[k] = std::min(size[k] - 1, std::max(c0, c1));
			}
			return box;
		}

		Handle<VoxelModel> Resized(const VoxelModel& m, int w, int h, int d, int dx, int dy,
		                           int dz) {
			CheckSize(w, h, d);
			Handle<VoxelModel> out = Handle<VoxelModel>::New(w, h, d);
			for (int x = 0; x < m.GetWidth(); x++)
				for (int y = 0; y < m.GetHeight(); y++)
					for (int z = 0; z < m.GetDepth(); z++) {
						if (!m.IsSolid(x, y, z))
							continue;
						const int nx = x + dx, ny = y + dy, nz = z + dz;
						if (nx < 0 || ny < 0 || nz < 0 || nx >= w || ny >= h || nz >= d)
							continue;
						out->SetSolid(nx, ny, nz, m.GetColor(x, y, z) & 0xFFFFFF);
					}
			// The pivot stays on the same voxels, as the editor keeps it.
			out->SetOrigin(m.GetOrigin() - MakeVector3(float(dx), float(dy), float(dz)));
			return out;
		}

		void Need(const std::vector<std::string>& a, std::size_t count, const char* usage) {
			if (a.size() != count)
				throw UsageError(std::string("usage: ") + usage);
		}

		// One edit of `model` (`a` is the command and its arguments, without the
		// file). Returns false when `a` is no edit. Throws on a bad edit.
		bool Edit(Handle<VoxelModel>& model, const std::vector<std::string>& a) {
			VoxelModel& m = *model;
			const std::string& cmd = a[0];
			if (cmd == "add-voxel" || cmd == "paint-voxel") {
				Need(a, 5, (cmd + " X Y Z COLOR").c_str());
				const int x = ParseInt(a[1], "X"), y = ParseInt(a[2], "Y"), z = ParseInt(a[3], "Z");
				const std::uint32_t color = ParseColor(a[4]);
				CheckInside(m, x, y, z);
				if (cmd == "add-voxel" || m.IsSolid(x, y, z))
					m.SetSolid(x, y, z, color);
				return true;
			}
			if (cmd == "remove-voxel") {
				Need(a, 4, "remove-voxel X Y Z");
				const int x = ParseInt(a[1], "X"), y = ParseInt(a[2], "Y"), z = ParseInt(a[3], "Z");
				CheckInside(m, x, y, z);
				m.SetAir(x, y, z);
				return true;
			}
			if (cmd == "fill-box" || cmd == "paint-box" || cmd == "clear-box") {
				const bool clear = cmd == "clear-box";
				Need(a, clear ? 7 : 8, (cmd + " X0 Y0 Z0 X1 Y1 Z1" + (clear ? "" : " COLOR")).c_str());
				const Box box = ParseBox(m, a, 1);
				const std::uint32_t color = clear ? 0 : ParseColor(a[7]);
				if (box.Empty())
					return true;
				for (int x = box.lo[0]; x <= box.hi[0]; x++)
					for (int y = box.lo[1]; y <= box.hi[1]; y++)
						for (int z = box.lo[2]; z <= box.hi[2]; z++) {
							if (clear)
								m.SetAir(x, y, z);
							else if (cmd == "fill-box" || m.IsSolid(x, y, z))
								m.SetSolid(x, y, z, color);
						}
				return true;
			}
			if (cmd == "set-pivot") {
				Need(a, 4, "set-pivot X Y Z");
				m.SetOrigin(MakeVector3(-ParseFloat(a[1], "X"), -ParseFloat(a[2], "Y"),
				                        -ParseFloat(a[3], "Z")));
				return true;
			}
			if (cmd == "resize") {
				if (a.size() != 4 && a.size() != 7)
					throw UsageError("usage: resize W H D [DX DY DZ]");
				const int w = ParseInt(a[1], "W"), h = ParseInt(a[2], "H"), d = ParseInt(a[3], "D");
				int dx = 0, dy = 0, dz = 0;
				if (a.size() == 7) {
					dx = ParseInt(a[4], "DX");
					dy = ParseInt(a[5], "DY");
					dz = ParseInt(a[6], "DZ");
				}
				model = Resized(m, w, h, d, dx, dy, dz);
				return true;
			}
			if (cmd == "trim") {
				Need(a, 1, "trim");
				int lo[3], hi[3];
				if (!Bounds(m, lo, hi))
					throw std::runtime_error("the model holds no voxel to trim to");
				model = Resized(m, hi[0] - lo[0] + 1, hi[1] - lo[1] + 1, hi[2] - lo[2] + 1, -lo[0],
				                -lo[1], -lo[2]);
				return true;
			}
			return false;
		}

		std::vector<std::string> SplitWords(const std::string& line) {
			std::vector<std::string> words;
			std::istringstream in(line);
			std::string word;
			while (in >> word)
				words.push_back(word);
			return words;
		}

		int Run(const std::vector<std::string>& args) {
			if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
				std::fputs(kUsage, stdout);
				return args.empty() ? 2 : 0;
			}
			const std::string& cmd = args[0];
			if (args.size() < 2)
				throw UsageError(cmd + " needs a FILE");
			const std::string& path = args[1];
			// What follows the file, with the command in front: the shape Edit takes.
			std::vector<std::string> rest{cmd};
			rest.insert(rest.end(), args.begin() + 2, args.end());

			if (cmd == "new") {
				bool force = false;
				std::vector<std::string> a;
				bool hasPivot = false;
				float pivot[3] = {0, 0, 0};
				for (std::size_t i = 1; i < rest.size(); i++) {
					if (rest[i] == "--force") {
						force = true;
					} else if (rest[i] == "--pivot") {
						if (i + 3 >= rest.size())
							throw UsageError("--pivot takes X Y Z");
						for (int k = 0; k < 3; k++)
							pivot[k] = ParseFloat(rest[i + 1 + k], "the pivot");
						hasPivot = true;
						i += 3;
					} else {
						a.push_back(rest[i]);
					}
				}
				if (a.size() != 3)
					throw UsageError("usage: new FILE W H D [--pivot X Y Z] [--force]");
				const int w = ParseInt(a[0], "W"), h = ParseInt(a[1], "H"), d = ParseInt(a[2], "D");
				CheckSize(w, h, d);
				if (!force && LocalFileSystem::Exists(path))
					throw std::runtime_error(path + " exists already (--force to replace it)");
				Handle<VoxelModel> model = Handle<VoxelModel>::New(w, h, d);
				if (!hasPivot) {
					pivot[0] = float(w / 2);
					pivot[1] = float(h / 2);
					pivot[2] = float(d / 2);
				}
				model->SetOrigin(MakeVector3(-pivot[0], -pivot[1], -pivot[2]));
				Save(*model, path);
				return 0;
			}

			Handle<VoxelModel> model = Load(path);
			const VoxelModel& m = *model;

			if (cmd == "info") {
				const bool json = rest.size() == 2 && rest[1] == "--json";
				if (rest.size() != 1 && !json)
					throw UsageError("usage: info FILE [--json]");
				const Vector3 p = m.GetOrigin() * -1.0F;
				int lo[3], hi[3];
				const bool any = Bounds(m, lo, hi);
				const int count = CountSolid(m);
				if (json) {
					std::printf("{\"size\": [%d, %d, %d], \"pivot\": [%s, %s, %s], \"voxels\": %d, "
					            "\"bounds\": ",
					            m.GetWidth(), m.GetHeight(), m.GetDepth(), FormatFloat(p.x).c_str(),
					            FormatFloat(p.y).c_str(), FormatFloat(p.z).c_str(), count);
					if (any)
						std::printf("{\"min\": [%d, %d, %d], \"max\": [%d, %d, %d]}}\n", lo[0], lo[1],
						            lo[2], hi[0], hi[1], hi[2]);
					else
						std::printf("null}\n");
				} else {
					std::printf("size %d %d %d\n", m.GetWidth(), m.GetHeight(), m.GetDepth());
					std::printf("pivot %s %s %s\n", FormatFloat(p.x).c_str(),
					            FormatFloat(p.y).c_str(), FormatFloat(p.z).c_str());
					std::printf("voxels %d\n", count);
					if (any)
						std::printf("rect %d %d %d %d %d %d\n", lo[0], lo[1], lo[2], hi[0], hi[1],
						            hi[2]);
					else
						std::printf("rect empty\n");
				}
				return 0;
			}
			if (cmd == "get-size") {
				Need(rest, 1, "get-size FILE");
				std::printf("%d %d %d\n", m.GetWidth(), m.GetHeight(), m.GetDepth());
				return 0;
			}
			if (cmd == "get-pivot") {
				Need(rest, 1, "get-pivot FILE");
				const Vector3 p = m.GetOrigin() * -1.0F;
				std::printf("%s %s %s\n", FormatFloat(p.x).c_str(), FormatFloat(p.y).c_str(),
				            FormatFloat(p.z).c_str());
				return 0;
			}
			if (cmd == "get-rect") {
				Need(rest, 1, "get-rect FILE");
				int lo[3], hi[3];
				if (Bounds(m, lo, hi))
					std::printf("%d %d %d %d %d %d\n", lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
				else
					std::printf("empty\n");
				return 0;
			}
			if (cmd == "count-voxels") {
				Need(rest, 1, "count-voxels FILE");
				std::printf("%d\n", CountSolid(m));
				return 0;
			}
			if (cmd == "get-voxel") {
				Need(rest, 4, "get-voxel FILE X Y Z");
				const int x = ParseInt(rest[1], "X"), y = ParseInt(rest[2], "Y"),
				          z = ParseInt(rest[3], "Z");
				CheckInside(m, x, y, z);
				std::printf("%s\n", m.IsSolid(x, y, z) ? FormatColor(m.GetColor(x, y, z)).c_str()
				                                       : "air");
				return 0;
			}
			if (cmd == "list-voxels") {
				Need(rest, 1, "list-voxels FILE");
				for (int x = 0; x < m.GetWidth(); x++)
					for (int y = 0; y < m.GetHeight(); y++)
						for (int z = 0; z < m.GetDepth(); z++) {
							if (m.IsSolid(x, y, z))
								std::printf("%d %d %d %s\n", x, y, z,
								            FormatColor(m.GetColor(x, y, z)).c_str());
						}
				return 0;
			}
			if (cmd == "apply") {
				Need(rest, 1, "apply FILE < edits");
				std::string line;
				int number = 0;
				while (std::getline(std::cin, line)) {
					number++;
					const std::vector<std::string> words = SplitWords(line);
					if (words.empty() || words[0][0] == '#')
						continue;
					try {
						if (!Edit(model, words))
							throw UsageError("not an edit: '" + words[0] + "'");
					} catch (const std::exception& ex) {
						// Nothing is saved: a batch applies whole or not at all.
						throw std::runtime_error("line " + std::to_string(number) + ": " + ex.what());
					}
				}
				Save(*model, path);
				return 0;
			}
			if (!Edit(model, rest))
				throw UsageError("unknown command '" + cmd + "' (try help)");
			Save(*model, path);
			return 0;
		}
	} // namespace

	int RunKV6Tools(int argc, char** argv) {
		std::vector<std::string> args;
		for (int i = 0; i < argc; i++)
			args.push_back(argv[i] ? argv[i] : "");
		try {
			return Run(args);
		} catch (const UsageError& ex) {
			std::fprintf(stderr, "kv6-tools: %s\n", ex.what());
			return 2;
		} catch (const std::exception& ex) {
			std::fprintf(stderr, "kv6-tools: %s\n", ex.what());
			return 1;
		}
	}
} // namespace spades
