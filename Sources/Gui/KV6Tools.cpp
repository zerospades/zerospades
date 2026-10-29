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

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <Core/Bitmap.h>
#include <Core/Exception.h>
#include <Core/IBitmapCodec.h>
#include <Core/StdStream.h>
#include <Core/LocalFileSystem.h>
#include <Core/VoxelModel.h>
#include <Gui/UI/Editor/KV6/KV6ModelIO.h>

namespace spades {
	namespace {
		// Coordinates are voxel indices, 0 .. size - 1 on each axis, with Z
		// pointing down as in the game. Colours are RRGGBB in hex; the model
		// keeps them packed 0x00BBGGRR. The pivot is the point the model is
		// built about, as the editor shows it (the KV6 origin, negated).

		// A KV6 file keeps only the voxels with a face in the open: air sealed
		// inside a closed shell reads back solid, in this placeholder colour
		// (see VoxelModel::HollowFill). Such voxels are reported as "interior".
		constexpr std::uint32_t kInteriorColor = 0xDDBEEF;

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
		  "A KV6 file keeps only the voxels with a face in the open: air sealed\n"
		  "inside a closed shell (a hollow ball with no opening) reads back\n"
		  "solid. Those voxels look the same from outside, and are reported as\n"
		  "\"interior\" instead of a colour.\n"
		  "\n"
		  "  new FILE W H D [--pivot X Y Z] [--force]\n"
		  "                         create an empty W x H x D model (D <= 64), its\n"
		  "                         pivot in the middle unless given\n"
		  "  info FILE [--json]     size, pivot, voxel count (and how many are on\n"
		  "                         the surface) and bounds\n"
		  "  get-size FILE          W H D\n"
		  "  get-pivot FILE         X Y Z\n"
		  "  set-pivot FILE X Y Z   move the pivot (the voxels stay)\n"
		  "  get-rect FILE          MINX MINY MINZ MAXX MAXY MAXZ of the solid\n"
		  "                         voxels, or \"empty\"\n"
		  "  count-voxels FILE      how many voxels are solid\n"
		  "  get-voxel FILE X Y Z   RRGGBB, \"interior\" or \"air\"\n"
		  "  add-voxel FILE X Y Z COLOR\n"
		  "                         make a voxel solid in COLOR (replacing it)\n"
		  "  remove-voxel FILE X Y Z\n"
		  "  paint-voxel FILE X Y Z COLOR\n"
		  "                         recolour a solid voxel; air stays air\n"
		  "\n"
		  "Shapes, each drawn by a verb: fill-SHAPE makes its voxels solid in\n"
		  "COLOR, clear-SHAPE carves them out (it takes no COLOR), paint-SHAPE\n"
		  "recolours those that are solid (draw- and carve- are fill- and clear-).\n"
		  "Centres and radii may be fractional (4.5 centres on a voxel edge); a\n"
		  "prism's base is at X Y Z and it runs HEIGHT voxels along --axis (z by\n"
		  "default, which points down); --hollow T keeps only a wall T thick\n"
		  "(a closed shell reads back solid inside, see above: cut its opening\n"
		  "in the same apply batch, before it is first saved).\n"
		  "Whatever falls outside the model is left out.\n"
		  "  fill-box FILE X0 Y0 Z0 X1 Y1 Z1 [--hollow T] COLOR   (rect too)\n"
		  "  fill-hollow-box FILE X0 Y0 Z0 X1 Y1 Z1 T COLOR\n"
		  "  fill-cylinder FILE X Y Z RADIUS HEIGHT [--axis A] [--hollow T] COLOR\n"
		  "  fill-hollow-cylinder FILE X Y Z RADIUS HEIGHT T [--axis A] COLOR\n"
		  "  fill-hex-cylinder FILE X Y Z RADIUS HEIGHT [--axis A] [--hollow T] COLOR\n"
		  "  fill-hollow-hex-cylinder FILE X Y Z RADIUS HEIGHT T [--axis A] COLOR\n"
		  "                         a hexagonal prism, RADIUS to its corners\n"
		  "  fill-ball FILE X Y Z RADIUS [--hollow T] COLOR\n"
		  "  fill-hollow-ball FILE X Y Z RADIUS T COLOR\n"
		  "\n"
		  "  blit FILE SOURCE X Y Z [--pivot] [--over|--under|--carve|--paint]\n"
		  "                         copy the solid voxels of the model SOURCE in,\n"
		  "                         its voxel 0 0 0 at X Y Z (its pivot, with\n"
		  "                         --pivot): over what is there (the default),\n"
		  "                         under it (only into air), carving them out, or\n"
		  "                         painting only what is solid\n"
		  "\n"
		  "  list-voxels FILE [--surface]\n"
		  "                         one solid voxel per line: X Y Z RRGGBB (or\n"
		  "                         interior); --surface lists only those with a\n"
		  "                         face in the open\n"
		  "  resize FILE W H D [DX DY DZ]\n"
		  "                         new size, voxels moved by DX DY DZ (default 0);\n"
		  "                         the pivot stays on the same voxels; what falls\n"
		  "                         outside is lost\n"
		  "  trim FILE              shrink the volume to the solid voxels\n"
		  "  rotate FILE AXIS TURNS turn by TURNS quarter turns about x, y or z\n"
		  "                         (right-handed; negative turns the other way),\n"
		  "                         as the editor's Transform tool turns; the volume\n"
		  "                         and the pivot turn with the voxels\n"
		  "  mirror FILE AXIS       reflect across the middle of the volume\n"
		  "  move FILE DX DY DZ [--clip]\n"
		  "  move FILE up|down|left|right|forward|back [N] [--clip]\n"
		  "                         shift the voxels (N, default 1) while the pivot\n"
		  "                         stays, which moves the model as the game places\n"
		  "                         it; up is -Z, forward -Y. Refused when voxels\n"
		  "                         would leave the volume, unless --clip drops them\n"
		  "  apply FILE             run edits read from standard input, one per\n"
		  "                         line, then save once: any edit above without\n"
		  "                         its FILE (\"add-voxel 1 2 3 ff0000\"); blank\n"
		  "                         lines and lines starting with # are skipped\n"
		  "  render FILE VIEW OUT.png [--scale N] [--shade] [--background RRGGBB]\n"
		  "                         draw the model to a PNG (transparent where\n"
		  "                         there is no voxel, unless --background).\n"
		  "                         Face views look along one axis from the side\n"
		  "                         named, one pixel per voxel (times --scale):\n"
		  "                         +x right, -x left, +y back, -y front, -z top,\n"
		  "                         +z bottom; --shade darkens what lies deeper.\n"
		  "                         iso, iso+x+y, iso+x-y, iso-x-y, iso-x+y look\n"
		  "                         from above that corner (--scale: half a\n"
		  "                         voxel's width in pixels, default 4). It prints\n"
		  "                         which way the columns and rows run.\n"
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

		// Whether a solid voxel has a face in the open, so a KV6 file keeps it.
		bool OnSurface(const VoxelModel& m, int x, int y, int z) {
			return !m.IsSolid(x - 1, y, z) || !m.IsSolid(x + 1, y, z) || !m.IsSolid(x, y - 1, z) ||
			       !m.IsSolid(x, y + 1, z) || !m.IsSolid(x, y, z - 1) || !m.IsSolid(x, y, z + 1);
		}

		std::string VoxelText(const VoxelModel& m, int x, int y, int z) {
			const std::uint32_t color = m.GetColor(x, y, z) & 0xFFFFFF;
			return color == kInteriorColor ? std::string("interior") : FormatColor(color);
		}

		int CountSurface(const VoxelModel& m) {
			int count = 0;
			for (int x = 0; x < m.GetWidth(); x++)
				for (int y = 0; y < m.GetHeight(); y++)
					for (int z = 0; z < m.GetDepth(); z++)
						count += (m.IsSolid(x, y, z) && OnSurface(m, x, y, z)) ? 1 : 0;
			return count;
		}

		int CountSolid(const VoxelModel& m) {
			int count = 0;
			for (int x = 0; x < m.GetWidth(); x++)
				for (int y = 0; y < m.GetHeight(); y++)
					for (int z = 0; z < m.GetDepth(); z++)
						count += m.IsSolid(x, y, z) ? 1 : 0;
			return count;
		}

		// --- Shapes ----------------------------------------------------------
		//
		// Every shape is drawn by one of three verbs: fill (make its voxels solid
		// in a colour), clear (carve them out) and paint (recolour those of them
		// that are solid). A shape is a test of whether a voxel centre lies in
		// it, over the box it fits in; whatever of it lies outside the model is
		// left out.

		struct Shape {
			int lo[3], hi[3]; // the voxels it may hold, before clipping
			std::function<bool(float, float, float)> contains;
		};

		enum class Verb { Fill, Clear, Paint };

		// Positional arguments, and the options among them.
		struct ShapeArgs {
			std::vector<std::string> values;
			int axis = 2; // a prism's, along which its height runs
			float hollow = 0.0F; // wall thickness; 0 for a solid shape
		};

		ShapeArgs SplitShapeArgs(const std::vector<std::string>& a, std::size_t from) {
			ShapeArgs out;
			for (std::size_t i = from; i < a.size(); i++) {
				if (a[i] == "--axis") {
					if (i + 1 >= a.size())
						throw UsageError("--axis takes x, y or z");
					const std::string& v = a[++i];
					if (v == "x" || v == "X")
						out.axis = 0;
					else if (v == "y" || v == "Y")
						out.axis = 1;
					else if (v == "z" || v == "Z")
						out.axis = 2;
					else
						throw UsageError("--axis takes x, y or z, not '" + v + "'");
				} else if (a[i] == "--hollow") {
					if (i + 1 >= a.size())
						throw UsageError("--hollow takes a wall thickness");
					out.hollow = ParseFloat(a[++i], "the wall thickness");
					if (out.hollow <= 0.0F)
						throw UsageError("a wall is thicker than nothing");
				} else {
					out.values.push_back(a[i]);
				}
			}
			return out;
		}

		// What each shape takes, after the verb's colour comes off the end.
		const char* ShapeUsage(const std::string& shape) {
			if (shape == "box" || shape == "rect")
				return "X0 Y0 Z0 X1 Y1 Z1 [--hollow T]";
			if (shape == "hollow-box")
				return "X0 Y0 Z0 X1 Y1 Z1 T";
			if (shape == "cylinder" || shape == "hex-cylinder")
				return "X Y Z RADIUS HEIGHT [--axis x|y|z] [--hollow T]";
			if (shape == "hollow-cylinder" || shape == "hollow-hex-cylinder")
				return "X Y Z RADIUS HEIGHT T [--axis x|y|z]";
			if (shape == "ball")
				return "X Y Z RADIUS [--hollow T]";
			if (shape == "hollow-ball")
				return "X Y Z RADIUS T";
			return nullptr;
		}

		// The shape `name` from its arguments (the colour already taken off).
		Shape MakeShape(std::string name, ShapeArgs args) {
			const std::string usage = name + " " + ShapeUsage(name);
			// A hollow-* name is its solid shape with a wall, given last.
			if (name.compare(0, 7, "hollow-") == 0) {
				if (args.values.empty())
					throw UsageError("usage: " + usage);
				args.hollow = ParseFloat(args.values.back(), "the wall thickness");
				if (args.hollow <= 0.0F)
					throw UsageError("a wall is thicker than nothing");
				args.values.pop_back();
				name = name.substr(7);
			}
			const std::vector<std::string>& v = args.values;
			const float t = args.hollow;
			Shape shape;

			if (name == "box" || name == "rect") {
				if (v.size() != 6)
					throw UsageError("usage: " + usage);
				for (int k = 0; k < 3; k++) {
					const int c0 = ParseInt(v[k], "a corner"), c1 = ParseInt(v[3 + k], "a corner");
					shape.lo[k] = std::min(c0, c1);
					shape.hi[k] = std::max(c0, c1);
				}
				const Shape outer = shape;
				// A wall of `t` voxels: whatever is that close to a face.
				shape.contains = [outer, t](float x, float y, float z) {
					if (t <= 0.0F)
						return true;
					const float p[3] = {x, y, z};
					for (int k = 0; k < 3; k++) {
						if (p[k] - float(outer.lo[k]) < t || float(outer.hi[k]) - p[k] < t)
							return true;
					}
					return false;
				};
				return shape;
			}

			if (name == "ball") {
				if (v.size() != 4)
					throw UsageError("usage: " + usage);
				const float c[3] = {ParseFloat(v[0], "X"), ParseFloat(v[1], "Y"),
				                    ParseFloat(v[2], "Z")};
				const float r = ParseFloat(v[3], "the radius");
				if (r <= 0.0F)
					throw UsageError("a radius is larger than nothing");
				for (int k = 0; k < 3; k++) {
					shape.lo[k] = int(std::floor(c[k] - r));
					shape.hi[k] = int(std::ceil(c[k] + r));
				}
				const float inner = std::max(0.0F, r - t);
				shape.contains = [=](float x, float y, float z) {
					const float dx = x - c[0], dy = y - c[1], dz = z - c[2];
					const float d2 = dx * dx + dy * dy + dz * dz;
					return d2 <= r * r && (t <= 0.0F || d2 > inner * inner);
				};
				return shape;
			}

			if (name == "cylinder" || name == "hex-cylinder") {
				if (v.size() != 5)
					throw UsageError("usage: " + usage);
				const float c[3] = {ParseFloat(v[0], "X"), ParseFloat(v[1], "Y"),
				                    ParseFloat(v[2], "Z")};
				const float r = ParseFloat(v[3], "the radius");
				const int height = ParseInt(v[4], "the height");
				if (r <= 0.0F || height < 1)
					throw UsageError("a radius and a height are larger than nothing");
				const int axis = args.axis;
				// The two directions across the axis, as a reference image's.
				const int u = axis == 0 ? 1 : 0;
				const int w = axis == 2 ? 1 : 2;
				// The base is at the given point, and the prism runs `height`
				// voxels from it along its axis.
				const int base = int(std::lround(c[axis]));
				shape.lo[axis] = base;
				shape.hi[axis] = base + height - 1;
				for (int k : {u, w}) {
					shape.lo[k] = int(std::floor(c[k] - r));
					shape.hi[k] = int(std::ceil(c[k] + r));
				}
				const bool hex = name == "hex-cylinder";
				// A hexagon of circumradius `r` with two corners on the first
				// direction across; its wall `t` thick measured across its sides.
				const float s3 = std::sqrt(3.0F);
				auto inHex = [s3](float a, float b, float radius) {
					a = std::fabs(a);
					b = std::fabs(b);
					const float apothem = radius * s3 * 0.5F;
					return b <= apothem && a * s3 * 0.5F + b * 0.5F <= apothem;
				};
				const float innerCircle = std::max(0.0F, r - t);
				const float innerHex = std::max(0.0F, r - t * 2.0F / s3);
				shape.contains = [=](float x, float y, float z) {
					const float p[3] = {x, y, z};
					const float a = p[u] - c[u], b = p[w] - c[w];
					if (hex)
						return inHex(a, b, r) && (t <= 0.0F || !inHex(a, b, innerHex));
					const float d2 = a * a + b * b;
					return d2 <= r * r && (t <= 0.0F || d2 > innerCircle * innerCircle);
				};
				return shape;
			}
			throw UsageError("unknown shape '" + name + "'");
		}

		// Draws `shape` into `m` by `verb`; returns how many voxels it changed.
		int Draw(VoxelModel& m, const Shape& shape, Verb verb, std::uint32_t color) {
			const int size[3] = {m.GetWidth(), m.GetHeight(), m.GetDepth()};
			int lo[3], hi[3];
			for (int k = 0; k < 3; k++) {
				lo[k] = std::max(0, shape.lo[k]);
				hi[k] = std::min(size[k] - 1, shape.hi[k]);
			}
			int changed = 0;
			for (int x = lo[0]; x <= hi[0]; x++)
				for (int y = lo[1]; y <= hi[1]; y++)
					for (int z = lo[2]; z <= hi[2]; z++) {
						if (!shape.contains(float(x), float(y), float(z)))
							continue;
						const bool solid = m.IsSolid(x, y, z);
						if (verb == Verb::Clear) {
							if (solid) {
								m.SetAir(x, y, z);
								changed++;
							}
						} else if (verb == Verb::Fill || solid) {
							if (!solid || (m.GetColor(x, y, z) & 0xFFFFFF) != color)
								changed++;
							m.SetSolid(x, y, z, color);
						}
					}
			return changed;
		}

		// The verb and the shape a command like "fill-hollow-ball" names; false
		// when it names no shape.
		bool ShapeCommand(const std::string& cmd, Verb& verb, std::string& shape) {
			const std::size_t dash = cmd.find('-');
			if (dash == std::string::npos)
				return false;
			const std::string head = cmd.substr(0, dash);
			if (head == "fill" || head == "draw")
				verb = Verb::Fill;
			else if (head == "clear" || head == "carve")
				verb = Verb::Clear;
			else if (head == "paint")
				verb = Verb::Paint;
			else
				return false;
			shape = cmd.substr(dash + 1);
			return ShapeUsage(shape) != nullptr;
		}

		// --- Blit -------------------------------------------------------------

		enum class BlitMode { Over, Under, Carve, Paint };

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

		int ParseAxis(const std::string& v) {
			if (v == "x" || v == "X")
				return 0;
			if (v == "y" || v == "Y")
				return 1;
			if (v == "z" || v == "Z")
				return 2;
			throw UsageError("an axis is x, y or z, not '" + v + "'");
		}

		// `m` turned by `turns` right-handed quarter turns about `axis`, as the
		// editor's Transform tool turns voxels: the axis after `axis` goes onto
		// the one after that. The volume turns with them, and so does the pivot.
		Handle<VoxelModel> Turned(const VoxelModel& m, int axis, int turns) {
			turns = ((turns % 4) + 4) % 4;
			const int u = (axis + 1) % 3, v = (axis + 2) % 3;
			// One quarter turn takes (u, v) to (-v, u); in indices of the turned
			// volume, shifted back to start at 0, that is (size v - 1 - v, u).
			// `size` is the volume's before the turn, and becomes it after.
			auto turn = [u, v](float p[3], int size[3]) {
				const float pu = p[u];
				p[u] = float(size[v] - 1) - p[v];
				p[v] = pu;
				std::swap(size[u], size[v]);
			};
			const int start[3] = {m.GetWidth(), m.GetHeight(), m.GetDepth()};
			int size[3] = {start[0], start[1], start[2]};
			if (turns % 2)
				std::swap(size[u], size[v]);
			if (size[2] > kMaxDepth)
				throw std::runtime_error("turned so, the model would be " +
				                         std::to_string(size[2]) +
				                         " voxels deep, more than a KV6 holds (" +
				                         std::to_string(kMaxDepth) + ")");
			Handle<VoxelModel> out = Handle<VoxelModel>::New(size[0], size[1], size[2]);
			for (int x = 0; x < m.GetWidth(); x++)
				for (int y = 0; y < m.GetHeight(); y++)
					for (int z = 0; z < m.GetDepth(); z++) {
						if (!m.IsSolid(x, y, z))
							continue;
						float p[3] = {float(x), float(y), float(z)};
						int s[3] = {start[0], start[1], start[2]};
						for (int k = 0; k < turns; k++)
							turn(p, s);
						out->SetSolid(int(p[0]), int(p[1]), int(p[2]), m.GetColor(x, y, z) & 0xFFFFFF);
					}
			const Vector3 pivot = m.GetOrigin() * -1.0F;
			float p[3] = {pivot.x, pivot.y, pivot.z};
			int s[3] = {start[0], start[1], start[2]};
			for (int k = 0; k < turns; k++)
				turn(p, s);
			out->SetOrigin(MakeVector3(-p[0], -p[1], -p[2]));
			return out;
		}

		// `m` reflected across the middle of its volume along `axis`; the pivot
		// is reflected with it.
		Handle<VoxelModel> Mirrored(const VoxelModel& m, int axis) {
			Handle<VoxelModel> out =
			  Handle<VoxelModel>::New(m.GetWidth(), m.GetHeight(), m.GetDepth());
			const int size[3] = {m.GetWidth(), m.GetHeight(), m.GetDepth()};
			for (int x = 0; x < m.GetWidth(); x++)
				for (int y = 0; y < m.GetHeight(); y++)
					for (int z = 0; z < m.GetDepth(); z++) {
						if (!m.IsSolid(x, y, z))
							continue;
						int p[3] = {x, y, z};
						p[axis] = size[axis] - 1 - p[axis];
						out->SetSolid(p[0], p[1], p[2], m.GetColor(x, y, z) & 0xFFFFFF);
					}
			Vector3 pivot = m.GetOrigin() * -1.0F;
			float* q = &pivot.x;
			q[axis] = float(size[axis] - 1) - q[axis];
			out->SetOrigin(pivot * -1.0F);
			return out;
		}

		// The direction a name of one stands for. Z points down, so up is -Z;
		// the front is at -Y, as the editor's front reference image is seen.
		bool Direction(const std::string& name, int d[3]) {
			struct Named {
				const char* name;
				int x, y, z;
			};
			static const Named names[] = {{"up", 0, 0, -1},     {"down", 0, 0, 1},
			                              {"left", -1, 0, 0},   {"right", 1, 0, 0},
			                              {"forward", 0, -1, 0}, {"back", 0, 1, 0}};
			for (const Named& n : names) {
				if (name == n.name) {
					d[0] = n.x;
					d[1] = n.y;
					d[2] = n.z;
					return true;
				}
			}
			return false;
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
			Verb verb;
			std::string shapeName;
			if (ShapeCommand(cmd, verb, shapeName)) {
				ShapeArgs args = SplitShapeArgs(a, 1);
				std::uint32_t color = 0;
				if (verb != Verb::Clear) {
					if (args.values.empty())
						throw UsageError("usage: " + cmd + " " + ShapeUsage(shapeName) + " COLOR");
					color = ParseColor(args.values.back());
					args.values.pop_back();
				}
				try {
					Draw(m, MakeShape(shapeName, args), verb, color);
				} catch (const UsageError& ex) {
					// Said with the verb, and its colour when it takes one.
					const std::string text = ex.what();
					if (text.compare(0, 7, "usage: ") == 0)
						throw UsageError(std::string("usage: ") + cmd + " " +
						                 ShapeUsage(shapeName) + (verb == Verb::Clear ? "" : " COLOR"));
					throw;
				}
				return true;
			}
			if (cmd == "blit") {
				// blit SOURCE X Y Z [--pivot] [--under|--carve|--paint]
				std::vector<std::string> v;
				bool atPivot = false;
				BlitMode mode = BlitMode::Over;
				for (std::size_t i = 1; i < a.size(); i++) {
					if (a[i] == "--pivot")
						atPivot = true;
					else if (a[i] == "--under")
						mode = BlitMode::Under;
					else if (a[i] == "--carve")
						mode = BlitMode::Carve;
					else if (a[i] == "--paint")
						mode = BlitMode::Paint;
					else if (a[i] == "--over")
						mode = BlitMode::Over;
					else
						v.push_back(a[i]);
				}
				if (v.size() != 4)
					throw UsageError(
					  "usage: blit SOURCE X Y Z [--pivot] [--over|--under|--carve|--paint]");
				Handle<VoxelModel> source = Load(v[0]);
				const VoxelModel& src = *source;
				int at[3] = {ParseInt(v[1], "X"), ParseInt(v[2], "Y"), ParseInt(v[3], "Z")};
				// By default the source's voxel 0 0 0 lands at X Y Z; with --pivot
				// its pivot does, rounded to a voxel.
				if (atPivot) {
					const Vector3 pivot = src.GetOrigin() * -1.0F;
					at[0] -= int(std::lround(pivot.x));
					at[1] -= int(std::lround(pivot.y));
					at[2] -= int(std::lround(pivot.z));
				}
				for (int x = 0; x < src.GetWidth(); x++)
					for (int y = 0; y < src.GetHeight(); y++)
						for (int z = 0; z < src.GetDepth(); z++) {
							if (!src.IsSolid(x, y, z))
								continue;
							const int tx = x + at[0], ty = y + at[1], tz = z + at[2];
							if (tx < 0 || ty < 0 || tz < 0 || tx >= m.GetWidth() ||
							    ty >= m.GetHeight() || tz >= m.GetDepth())
								continue; // outside the model: left out
							const std::uint32_t color = src.GetColor(x, y, z) & 0xFFFFFF;
							const bool solid = m.IsSolid(tx, ty, tz);
							switch (mode) {
								case BlitMode::Over: m.SetSolid(tx, ty, tz, color); break;
								case BlitMode::Under:
									if (!solid)
										m.SetSolid(tx, ty, tz, color);
									break;
								case BlitMode::Carve: m.SetAir(tx, ty, tz); break;
								case BlitMode::Paint:
									if (solid)
										m.SetSolid(tx, ty, tz, color);
									break;
							}
						}
				return true;
			}
			if (cmd == "rotate") {
				Need(a, 3, "rotate AXIS TURNS");
				model = Turned(m, ParseAxis(a[1]), ParseInt(a[2], "the quarter turns"));
				return true;
			}
			if (cmd == "mirror") {
				Need(a, 2, "mirror AXIS");
				model = Mirrored(m, ParseAxis(a[1]));
				return true;
			}
			if (cmd == "move") {
				// move DX DY DZ | move DIRECTION [N], with --clip: the voxels
				// shift within the volume and the pivot stays, so the model moves
				// relative to it, as it is placed in the game.
				std::vector<std::string> v;
				bool clip = false;
				for (std::size_t i = 1; i < a.size(); i++) {
					if (a[i] == "--clip")
						clip = true;
					else
						v.push_back(a[i]);
				}
				int d[3] = {0, 0, 0};
				if ((v.size() == 1 || v.size() == 2) && Direction(v[0], d)) {
					const int n = v.size() == 2 ? ParseInt(v[1], "the voxels to move by") : 1;
					for (int& c : d)
						c *= n;
				} else if (v.size() == 3) {
					d[0] = ParseInt(v[0], "DX");
					d[1] = ParseInt(v[1], "DY");
					d[2] = ParseInt(v[2], "DZ");
				} else {
					throw UsageError("usage: move DX DY DZ [--clip], or move "
					                 "up|down|left|right|forward|back [N] [--clip]");
				}
				int lo[3], hi[3];
				const int size[3] = {m.GetWidth(), m.GetHeight(), m.GetDepth()};
				if (!clip && Bounds(m, lo, hi)) {
					for (int k = 0; k < 3; k++) {
						if (lo[k] + d[k] < 0 || hi[k] + d[k] >= size[k])
							throw std::runtime_error(
							  "moved so, voxels would leave the volume (resize it first, or "
							  "--clip to drop them)");
					}
				}
				const Vector3 origin = m.GetOrigin();
				model = Resized(m, size[0], size[1], size[2], d[0], d[1], d[2]);
				model->SetOrigin(origin); // the pivot stays where it was
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

		// --- Rendering ---------------------------------------------------------

		// A picture of a model, top row first, pixels packed 0xAABBGGRR.
		struct Picture {
			int width = 0, height = 0;
			std::vector<std::uint32_t> pixels;
			Picture(int w, int h, std::uint32_t fill)
			    : width(w), height(h), pixels(std::size_t(w) * std::size_t(h), fill) {}
			std::uint32_t& At(int x, int y) { return pixels[std::size_t(y) * width + x]; }
		};

		std::uint32_t Shaded(std::uint32_t packed, float light) {
			auto channel = [light](std::uint32_t c) {
				return std::uint32_t(std::min(255.0F, float(c) * light + 0.5F));
			};
			return channel(packed & 0xFF) | (channel((packed >> 8) & 0xFF) << 8) |
			       (channel((packed >> 16) & 0xFF) << 16) | 0xFF000000u;
		}

		void SavePng(const Picture& picture, int scale, const std::string& path) {
			IBitmapCodec* png = nullptr;
			for (IBitmapCodec* codec : IBitmapCodec::GetAllCodecs()) {
				if (codec->CanSave() && codec->CheckExtension(".png"))
					png = codec;
			}
			if (!png)
				throw std::runtime_error("this build cannot write PNG files");
			const int w = picture.width * scale, h = picture.height * scale;
			Handle<Bitmap> bitmap = Handle<Bitmap>::New(w, h);
			std::uint32_t* out = bitmap->GetPixels();
			// A bitmap keeps its bottom row first.
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++)
					out[std::size_t(h - 1 - y) * w + x] =
					  picture.pixels[std::size_t(y / scale) * picture.width + x / scale];
			std::FILE* f = LocalFileSystem::OpenFile(path, "wb");
			if (!f)
				throw std::runtime_error("cannot write " + path);
			StdStream stream(f, true); // takes ownership of the FILE*
			png->Save(&stream, bitmap.GetPointerOrNull());
		}

		// A face view: looking along one axis from the side `side` names, one
		// pixel per voxel, each the first solid voxel along the line of sight.
		// Rows run down Z in the side views and down Y from above or below, and
		// columns run to the right as a camera on that side sees it.
		struct FaceView {
			const char* name;
			const char* alias;
			int axis;     // looked along
			int toward;   // +1: from the low side, looking up the axis; -1: from the high side
			int column;   // axis the columns run along
			int columnSign;
			int row;      // axis the rows run down
			int rowSign;
		};
		const FaceView kFaceViews[] = {
		  {"+x", "right", 0, -1, 1, -1, 2, 1}, {"-x", "left", 0, 1, 1, 1, 2, 1},
		  {"+y", "back", 1, -1, 0, 1, 2, 1},   {"-y", "front", 1, 1, 0, -1, 2, 1},
		  {"-z", "top", 2, 1, 0, 1, 1, 1},     {"+z", "bottom", 2, -1, 0, -1, 1, 1},
		};

		std::string AxisLabel(int axis, int sign) {
			return std::string(sign > 0 ? "+" : "-") + "XYZ"[axis];
		}

		Picture RenderFace(const VoxelModel& m, const FaceView& view, bool shade,
		                   std::uint32_t background) {
			const int size[3] = {m.GetWidth(), m.GetHeight(), m.GetDepth()};
			Picture picture(size[view.column], size[view.row], background);
			const int depth = size[view.axis];
			for (int r = 0; r < picture.height; r++)
				for (int c = 0; c < picture.width; c++) {
					int p[3];
					p[view.column] = view.columnSign > 0 ? c : size[view.column] - 1 - c;
					p[view.row] = view.rowSign > 0 ? r : size[view.row] - 1 - r;
					for (int d = 0; d < depth; d++) {
						p[view.axis] = view.toward > 0 ? d : depth - 1 - d;
						if (!m.IsSolid(p[0], p[1], p[2]))
							continue;
						// Deeper voxels darken with --shade, to half at the far end.
						const float light =
						  shade && depth > 1 ? 1.0F - 0.5F * float(d) / float(depth - 1) : 1.0F;
						picture.At(c, r) = Shaded(m.GetColor(p[0], p[1], p[2]) & 0xFFFFFF, light);
						break;
					}
				}
			return picture;
		}

		// An isometric view from above one corner: `turns` quarter turns about
		// Z bring that corner to +X+Y, the one drawn. `a` is half a voxel's
		// width on screen, in pixels; a voxel is `a` pixels tall.
		Picture RenderIso(const VoxelModel& source, int turns, int a, std::uint32_t background) {
			Handle<VoxelModel> turned = Turned(source, 2, turns);
			const VoxelModel& m = *turned;
			const int w = m.GetWidth(), h = m.GetHeight(), d = m.GetDepth();
			// From above +X+Y, the screen's right is +X-Y and its down is +X+Y and
			// +Z; nearer to the eye is larger X + Y - Z.
			auto project = [a, h](float x, float y, float z) {
				return MakeVector2((x - y + float(h)) * float(a), ((x + y) * 0.5F + z) * float(a));
			};
			Picture picture((w + h) * a, int(std::ceil((float(w + h) * 0.5F + float(d)) * float(a))),
			                background);
			struct Voxel {
				int x, y, z;
			};
			std::vector<Voxel> voxels;
			for (int x = 0; x < w; x++)
				for (int y = 0; y < h; y++)
					for (int z = 0; z < d; z++) {
						if (m.IsSolid(x, y, z) &&
						    (!m.IsSolid(x + 1, y, z) || !m.IsSolid(x, y + 1, z) ||
						     !m.IsSolid(x, y, z - 1)))
							voxels.push_back({x, y, z});
					}
			// Far to near, so nearer faces paint over farther ones.
			std::stable_sort(voxels.begin(), voxels.end(), [](const Voxel& p, const Voxel& q) {
				return p.x + p.y - p.z < q.x + q.y - q.z;
			});
			auto fill = [&](const Vector2 c[4], std::uint32_t color) {
				float minX = c[0].x, maxX = c[0].x, minY = c[0].y, maxY = c[0].y;
				for (int i = 1; i < 4; i++) {
					minX = std::min(minX, c[i].x);
					maxX = std::max(maxX, c[i].x);
					minY = std::min(minY, c[i].y);
					maxY = std::max(maxY, c[i].y);
				}
				for (int py = std::max(0, int(std::floor(minY))); py < std::min(picture.height, int(std::ceil(maxY))); py++)
					for (int px = std::max(0, int(std::floor(minX))); px < std::min(picture.width, int(std::ceil(maxX))); px++) {
						const float sx = float(px) + 0.5F, sy = float(py) + 0.5F;
						// Inside the convex quad when on the same side of each edge.
						bool positive = false, negative = false;
						for (int i = 0; i < 4; i++) {
							const Vector2& p = c[i];
							const Vector2& q = c[(i + 1) % 4];
							const float cross = (q.x - p.x) * (sy - p.y) - (q.y - p.y) * (sx - p.x);
							positive = positive || cross > 0.0F;
							negative = negative || cross < 0.0F;
						}
						if (!(positive && negative))
							picture.At(px, py) = color;
					}
			};
			for (const Voxel& v : voxels) {
				const float x = float(v.x), y = float(v.y), z = float(v.z);
				const std::uint32_t color = m.GetColor(v.x, v.y, v.z) & 0xFFFFFF;
				if (!m.IsSolid(v.x, v.y, v.z - 1)) { // the top, lit most
					const Vector2 c[4] = {project(x, y, z), project(x + 1, y, z),
					                      project(x + 1, y + 1, z), project(x, y + 1, z)};
					fill(c, Shaded(color, 1.0F));
				}
				if (!m.IsSolid(v.x + 1, v.y, v.z)) {
					const Vector2 c[4] = {project(x + 1, y, z), project(x + 1, y + 1, z),
					                      project(x + 1, y + 1, z + 1), project(x + 1, y, z + 1)};
					fill(c, Shaded(color, 0.8F));
				}
				if (!m.IsSolid(v.x, v.y + 1, v.z)) {
					const Vector2 c[4] = {project(x, y + 1, z), project(x + 1, y + 1, z),
					                      project(x + 1, y + 1, z + 1), project(x, y + 1, z + 1)};
					fill(c, Shaded(color, 0.62F));
				}
			}
			return picture;
		}

		int Render(const VoxelModel& m, const std::vector<std::string>& a) {
			// a: render VIEW OUT.png [--scale N] [--shade] [--background RRGGBB]
			std::vector<std::string> v;
			int scale = 0;
			bool shade = false;
			std::uint32_t background = 0; // transparent
			for (std::size_t i = 1; i < a.size(); i++) {
				if (a[i] == "--shade") {
					shade = true;
				} else if (a[i] == "--scale") {
					if (i + 1 >= a.size())
						throw UsageError("--scale takes a whole number");
					scale = ParseInt(a[++i], "the scale");
					if (scale < 1 || scale > 64)
						throw UsageError("a scale runs from 1 to 64");
				} else if (a[i] == "--background") {
					if (i + 1 >= a.size())
						throw UsageError("--background takes RRGGBB or transparent");
					const std::string& c = a[++i];
					background = c == "transparent" ? 0 : (ParseColor(c) | 0xFF000000u);
				} else {
					v.push_back(a[i]);
				}
			}
			if (v.size() != 2)
				throw UsageError("usage: render FILE VIEW OUT.png [--scale N] [--shade] "
				                 "[--background RRGGBB|transparent]");
			const std::string& view = v[0];
			const std::string& out = v[1];
			for (const FaceView& face : kFaceViews) {
				if (view != face.name && view != face.alias)
					continue;
				const Picture picture = RenderFace(m, face, shade, background);
				const int s = scale > 0 ? scale : 1;
				SavePng(picture, s, out);
				std::printf("%s %dx%d, columns %s, rows %s\n", out.c_str(), picture.width * s,
				            picture.height * s, AxisLabel(face.column, face.columnSign).c_str(),
				            AxisLabel(face.row, face.rowSign).c_str());
				return 0;
			}
			// The corners, from above, and the quarter turns about Z that bring
			// each to +X+Y.
			const struct {
				const char* name;
				int turns;
			} corners[] = {{"iso", 0}, {"iso+x+y", 0}, {"iso+x-y", 1}, {"iso-x-y", 2},
			               {"iso-x+y", 3}};
			for (const auto& corner : corners) {
				if (view != corner.name)
					continue;
				const Picture picture = RenderIso(m, corner.turns, scale > 0 ? scale : 4, background);
				SavePng(picture, 1, out);
				std::string from = corner.turns == 0 ? "+x+y" : corner.name + 3;
				for (char& ch : from)
					ch = char(std::toupper(static_cast<unsigned char>(ch)));
				std::printf("%s %dx%d, isometric from above %s\n", out.c_str(), picture.width,
				            picture.height, from.c_str());
				return 0;
			}
			throw UsageError("unknown view '" + view + "': +x -x +y -y +z -z (right left back "
			                 "front bottom top), or iso, iso+x+y, iso+x-y, iso-x-y, iso-x+y");
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
				const int surface = CountSurface(m);
				if (json) {
					std::printf("{\"size\": [%d, %d, %d], \"pivot\": [%s, %s, %s], \"voxels\": %d, "
					            "\"surfaceVoxels\": %d, \"bounds\": ",
					            m.GetWidth(), m.GetHeight(), m.GetDepth(), FormatFloat(p.x).c_str(),
					            FormatFloat(p.y).c_str(), FormatFloat(p.z).c_str(), count, surface);
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
					std::printf("surface %d\n", surface);
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
				std::printf("%s\n", m.IsSolid(x, y, z) ? VoxelText(m, x, y, z).c_str() : "air");
				return 0;
			}
			if (cmd == "list-voxels") {
				const bool surfaceOnly = rest.size() == 2 && rest[1] == "--surface";
				if (rest.size() != 1 && !surfaceOnly)
					throw UsageError("usage: list-voxels FILE [--surface]");
				for (int x = 0; x < m.GetWidth(); x++)
					for (int y = 0; y < m.GetHeight(); y++)
						for (int z = 0; z < m.GetDepth(); z++) {
							if (!m.IsSolid(x, y, z) || (surfaceOnly && !OnSurface(m, x, y, z)))
								continue;
							std::printf("%d %d %d %s\n", x, y, z, VoxelText(m, x, y, z).c_str());
						}
				return 0;
			}
			if (cmd == "render")
				return Render(m, rest);
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
