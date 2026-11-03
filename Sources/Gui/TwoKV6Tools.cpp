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

#include "TwoKV6Tools.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include <Core/Exception.h>
#include <Core/LocalFileSystem.h>
#include <Core/StdStream.h>
#include <Core/VoxelModel.h>
#include <Core/VoxelModel2KV6.h>
#include <Core/VoxelModelLoader.h>
#include <Gui/UI/Editor/KV6/KV6ModelIO.h>

#include "KV6Tools.h"
#include "ToolArgs.h"

namespace spades {
	namespace {
		using namespace toolargs;

		// Objects are named by their path of names from the top ("Body/Arm"),
		// or by the number `tree` gives them ("#3"). A hinge is an object with
		// no voxels of its own: a joint the objects under it turn about. Each
		// object's place is relative to its parent: a position, a turn (shown
		// and typed as degrees about X, then Y, then Z, each about the fixed
		// axes) and a scale. Z points down, as in the game.

		const char* const kUsage =
		  "usage: --2kv6-tools COMMAND FILE [ARGS...]\n"
		  "\n"
		  "Reads and edits .2kv6 scenes without opening the game: a tree of\n"
		  "objects, each with its own voxels (a model) or none (a hinge, a joint\n"
		  "what hangs under it turns about), placed relative to its parent.\n"
		  "OBJECT is a path of names from the top (Body/Arm) or the number tree\n"
		  "prints (#3); PARENT is one too, or / for the top. Turns are degrees\n"
		  "about X, then Y, then Z; Z points down, as in the game. A locked\n"
		  "object (a template's hinge) keeps its name, its parent, its turn and\n"
		  "its scale, and may only be moved or given voxels, until unlocked.\n"
		  "What hangs under an object follows it wherever it goes.\n"
		  "\n"
		  "  new FILE [--force]       an empty scene\n"
		  "  tree FILE [--world] [--json]\n"
		  "                           every object: number, name, model size or\n"
		  "                           hinge, position, turn, scale and flags;\n"
		  "                           --world gives where each sits in the scene\n"
		  "  info FILE                counts, and the size the game composes\n"
		  "\n"
		  "  add-model FILE PARENT MODEL [--name NAME] [--at X Y Z]\n"
		  "                           a .kv6 as one object, or a .2kv6's objects\n"
		  "                           (with all under them) merged in\n"
		  "  add-hinge FILE PARENT NAME [--at X Y Z] [--lock]\n"
		  "  set-model FILE OBJECT MODEL.kv6\n"
		  "                           give an object (or a hinge) these voxels\n"
		  "  clear-model FILE OBJECT  take its voxels, leaving a hinge\n"
		  "  extract FILE OBJECT OUT [--self]\n"
		  "                           OUT.2kv6: the object and all under it, where\n"
		  "                           it stands in the scene; OUT.kv6: its voxels\n"
		  "                           composed with all under it (--self: its own\n"
		  "                           only), its pivot on the object's origin\n"
		  "  flatten FILE OUT.kv6     the whole scene composed as the game draws\n"
		  "                           it, its pivot on the scene's origin\n"
		  "\n"
		  "  move FILE OBJECT DX DY DZ        set-position FILE OBJECT X Y Z\n"
		  "  rotate FILE OBJECT AXIS DEGREES  turn about its origin, in its\n"
		  "                                   parent's axes\n"
		  "  set-rotation FILE OBJECT RX RY RZ\n"
		  "  scale FILE OBJECT F|FX FY FZ     set-scale FILE OBJECT S|SX SY SZ\n"
		  "  reparent FILE OBJECT PARENT [--keep-local]\n"
		  "                           hang it under PARENT, staying where it is in\n"
		  "                           the scene (unless --keep-local)\n"
		  "  rename FILE OBJECT NAME\n"
		  "  remove FILE OBJECT [--force]\n"
		  "                           with all under it; --force for locked ones\n"
		  "  lock|unlock|hide|show FILE OBJECT\n"
		  "\n"
		  "  render FILE VIEW OUT.png [--object OBJECT] [--scale N] [--shade]\n"
		  "         [--background RRGGBB]\n"
		  "                           the scene (or one object and all under it)\n"
		  "                           as the game composes it, transparent where\n"
		  "                           there is no voxel. Views as --kv6-tools has\n"
		  "                           them: +x -x +y -y +z -z (right left back\n"
		  "                           front bottom top), iso, iso+x-y, iso-x-y,\n"
		  "                           iso-x+y\n"
		  "  apply FILE               run edits from standard input, one per line\n"
		  "                           without FILE, then save once (# comments)\n"
		  "  help                     this text\n";

		// --- Turns ---------------------------------------------------------

		Vector4 AxisAngle(int axis, float degrees) {
			const float half = degrees * float(M_PI) / 360.0F;
			const float s = std::sin(half);
			return MakeVector4(axis == 0 ? s : 0.0F, axis == 1 ? s : 0.0F, axis == 2 ? s : 0.0F,
			                   std::cos(half));
		}

		Vector4 Compose(const Vector4& a, const Vector4& b) {
			return (Quaternion(a) * Quaternion(b)).Normalize().v;
		}

		// About X, then Y, then Z, each about the fixed axes.
		Vector4 FromEuler(float rx, float ry, float rz) {
			return Compose(AxisAngle(2, rz), Compose(AxisAngle(1, ry), AxisAngle(0, rx)));
		}

		Vector3 ToEuler(const Vector4& q) {
			const float x = q.x, y = q.y, z = q.z, w = q.w;
			const float r00 = 1 - 2 * (y * y + z * z), r10 = 2 * (x * y + w * z);
			const float r20 = 2 * (x * z - w * y), r21 = 2 * (y * z + w * x);
			const float r22 = 1 - 2 * (x * x + y * y), r11 = 1 - 2 * (x * x + z * z);
			const float r12 = 2 * (y * z - w * x);
			const float toDeg = 180.0F / float(M_PI);
			const float sy = std::max(-1.0F, std::min(1.0F, -r20));
			if (std::fabs(sy) > 0.9999F) // looking straight along Y: X takes it all
				return MakeVector3(std::atan2(-r12, r11) * toDeg, std::asin(sy) * toDeg, 0.0F);
			return MakeVector3(std::atan2(r21, r22) * toDeg, std::asin(sy) * toDeg,
			                   std::atan2(r10, r00) * toDeg);
		}

		Matrix4 LocalTransform(const VoxelObject& o) {
			return Matrix4::Translate(o.position) * Quaternion(o.rotation).ToRotationMatrix() *
			       Matrix4::Scale(o.scale);
		}

		// The rotation of an orthonormal, right-handed frame, built from its
		// largest part so a half turn never divides by zero.
		Vector4 RotationOf(const Vector3& a, const Vector3& b, const Vector3& c) {
			const float r00 = a.x, r10 = a.y, r20 = a.z, r01 = b.x, r11 = b.y, r21 = b.z;
			const float r02 = c.x, r12 = c.y, r22 = c.z;
			const float trace = r00 + r11 + r22;
			Vector4 q;
			if (trace > 0.0F) {
				const float s = std::sqrt(trace + 1.0F) * 2.0F;
				q = MakeVector4((r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25F * s);
			} else if (r00 > r11 && r00 > r22) {
				const float s = std::sqrt(1.0F + r00 - r11 - r22) * 2.0F;
				q = MakeVector4(0.25F * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s);
			} else if (r11 > r22) {
				const float s = std::sqrt(1.0F + r11 - r00 - r22) * 2.0F;
				q = MakeVector4((r01 + r10) / s, 0.25F * s, (r12 + r21) / s, (r02 - r20) / s);
			} else {
				const float s = std::sqrt(1.0F + r22 - r00 - r11) * 2.0F;
				q = MakeVector4((r02 + r20) / s, (r12 + r21) / s, 0.25F * s, (r10 - r01) / s);
			}
			return q.Normalize();
		}

		// `m` back into a position, a turn and a scale; a shear, which those
		// cannot hold, is left out.
		void Decompose(const Matrix4& m, VoxelObject& o) {
			Vector3 axis[3] = {m.GetAxis(0), m.GetAxis(1), m.GetAxis(2)};
			float scale[3];
			for (int k = 0; k < 3; k++) {
				scale[k] = axis[k].GetLength();
				if (scale[k] > 0.0F)
					axis[k] = axis[k] * (1.0F / scale[k]);
			}
			if (Vector3::Dot(Vector3::Cross(axis[0], axis[1]), axis[2]) < 0.0F) {
				scale[0] = -scale[0]; // mirrored: one axis carries the reflection
				axis[0] = axis[0] * -1.0F;
			}
			o.position = m.GetOrigin();
			o.rotation = RotationOf(axis[0], axis[1], axis[2]);
			o.scale = MakeVector3(scale[0], scale[1], scale[2]);
		}

		// --- The tree ------------------------------------------------------

		using Scene = std::vector<VoxelObject>;

		// Where an object is: the list holding it, its place there, and the
		// object above it (null at the top).
		struct Ref {
			Scene* siblings = nullptr;
			std::size_t index = 0;
			VoxelObject* parent = nullptr;
			VoxelObject& Object() const { return (*siblings)[index]; }
		};

		// Every object, parents first, as `tree` numbers them from #1.
		void Walk(Scene& level, VoxelObject* parent,
		          const std::function<void(const Ref&, int depth)>& visit, int depth = 0) {
			for (std::size_t i = 0; i < level.size(); i++) {
				Ref ref{&level, i, parent};
				visit(ref, depth);
				Walk(level[i].children, &level[i], visit, depth + 1);
			}
		}

		Ref Find(Scene& scene, const std::string& path) {
			if (!path.empty() && path[0] == '#') {
				const int wanted = ParseInt(path.substr(1), "an object number");
				int n = 0;
				Ref found;
				Walk(scene, nullptr, [&](const Ref& ref, int) {
					if (++n == wanted)
						found = ref;
				});
				if (!found.siblings)
					throw std::runtime_error("no object " + path + " (see tree)");
				return found;
			}
			Scene* level = &scene;
			VoxelObject* parent = nullptr;
			Ref found;
			std::size_t start = (!path.empty() && path[0] == '/') ? 1 : 0;
			while (start <= path.size()) {
				const std::size_t slash = path.find('/', start);
				const std::string name =
				  path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
				std::size_t match = level->size(), matches = 0;
				for (std::size_t i = 0; i < level->size(); i++) {
					if ((*level)[i].name == name) {
						if (matches++ == 0)
							match = i;
					}
				}
				if (matches == 0)
					throw std::runtime_error("no object '" + path + "' (see tree)");
				if (matches > 1)
					throw std::runtime_error("'" + name +
					                         "' names several objects there: use its #number");
				found = Ref{level, match, parent};
				parent = &(*level)[match];
				level = &parent->children;
				if (slash == std::string::npos)
					break;
				start = slash + 1;
			}
			return found;
		}

		// The children a PARENT argument names: the top level for "/" or "".
		Scene& ChildrenOf(Scene& scene, const std::string& parent, VoxelObject** holder) {
			*holder = nullptr;
			if (parent.empty() || parent == "/")
				return scene;
			Ref ref = Find(scene, parent);
			*holder = &ref.Object();
			return ref.Object().children;
		}

		// What the parents of the object at `ref` impose on it.
		Matrix4 ParentTransform(Scene& scene, const VoxelObject* target) {
			Matrix4 result = Matrix4::Identity();
			std::function<bool(Scene&, const Matrix4&)> search = [&](Scene& level,
			                                                          const Matrix4& above) {
				for (VoxelObject& o : level) {
					if (&o == target) {
						result = above;
						return true;
					}
					if (search(o.children, above * LocalTransform(o)))
						return true;
				}
				return false;
			};
			search(scene, Matrix4::Identity());
			return result;
		}

		bool AnyLocked(const VoxelObject& o) {
			if (o.locked)
				return true;
			for (const VoxelObject& c : o.children)
				if (AnyLocked(c))
					return true;
			return false;
		}

		bool Contains(const VoxelObject& o, const VoxelObject* target) {
			if (&o == target)
				return true;
			for (const VoxelObject& c : o.children)
				if (Contains(c, target))
					return true;
			return false;
		}

		void RefuseLocked(const VoxelObject& o, const char* what) {
			if (o.locked)
				throw std::runtime_error("'" + o.name + "' is locked: unlock it to " + what + " it");
		}

		// "Name", "Name.001", ...: the first that none of `siblings` uses.
		std::string UnusedName(const Scene& siblings, const std::string& base,
		                       const VoxelObject* except = nullptr) {
			auto taken = [&](const std::string& name) {
				for (const VoxelObject& o : siblings)
					if (&o != except && o.name == name)
						return true;
				return false;
			};
			if (!taken(base))
				return base;
			for (int n = 1; n < 1000; n++) {
				char suffix[8];
				std::snprintf(suffix, sizeof(suffix), ".%03d", n);
				if (!taken(base + suffix))
					return base + suffix;
			}
			throw std::runtime_error("too many objects named '" + base + "' there");
		}

		void CheckName(const std::string& name) {
			if (name.empty() || name.find('/') != std::string::npos || name[0] == '#')
				throw UsageError("a name is not empty, holds no '/' and does not start with '#'");
		}

		// --- Files ---------------------------------------------------------

		bool IsScenePath(const std::string& path) { return LocalFileSystem::HasExtension(path, ".2kv6"); }

		Scene LoadScene(const std::string& path) {
			std::FILE* f = LocalFileSystem::OpenFile(path, "rb");
			if (!f)
				throw std::runtime_error("cannot open " + path);
			StdStream stream(f, true);
			return VoxelModel2KV6::Load(stream);
		}

		void SaveScene(const Scene& scene, const std::string& path) {
			// Through a temporary file, so a failure never leaves half a scene.
			const std::string tmp = path + ".savetmp";
			{
				std::FILE* f = LocalFileSystem::OpenFile(tmp, "wb");
				if (!f)
					throw std::runtime_error("cannot write " + path);
				StdStream stream(f, true);
				VoxelModel2KV6::Save(stream, scene);
			}
			if (!LocalFileSystem::Rename(tmp, path, true)) {
				LocalFileSystem::Delete(tmp);
				throw std::runtime_error("cannot write " + path);
			}
		}

		Handle<VoxelModel> LoadModel(const std::string& path) {
			Handle<VoxelModel> model = gui::KV6ModelIO::Load(path);
			if (!model)
				throw std::runtime_error("cannot read a KV6 model from " + path);
			return model;
		}

		// `objects` composed as the game composes a scene, with its pivot on
		// their origin.
		Handle<VoxelModel> Composed(const Scene& objects) {
			IntVector3 low;
			Handle<VoxelModel> model = VoxelModelLoader::MergeScene(objects, &low);
			model->SetOrigin(MakeVector3(float(low.x), float(low.y), float(low.z)));
			return model;
		}

		Vector3 ParseVector(const std::vector<std::string>& v, std::size_t at, const char* what) {
			return MakeVector3(ParseFloat(v[at], what), ParseFloat(v[at + 1], what),
			                   ParseFloat(v[at + 2], what));
		}

		// A number as it reads best: rounding noise from composing turns (and a
		// negative zero) shows as the round number it is.
		std::string Clean(float v) {
			v = std::round(v * 10000.0F) / 10000.0F;
			if (v == 0.0F)
				v = 0.0F; // no "-0"
			return FormatFloat(v);
		}

		std::string Format3(const Vector3& v, const char* separator = " ") {
			return Clean(v.x) + separator + Clean(v.y) + separator + Clean(v.z);
		}

		// Options taken out of `a`, the rest left in order.
		struct Options {
			std::vector<std::string> rest;
			std::vector<std::pair<std::string, std::vector<std::string>>> given;
			bool Has(const std::string& name) const {
				for (const auto& o : given)
					if (o.first == name)
						return true;
				return false;
			}
			const std::vector<std::string>& Values(const std::string& name) const {
				for (const auto& o : given)
					if (o.first == name)
						return o.second;
				static const std::vector<std::string> none;
				return none;
			}
		};
		Options TakeOptions(const std::vector<std::string>& a, std::size_t from,
		                    const std::vector<std::pair<std::string, int>>& known) {
			Options out;
			for (std::size_t i = from; i < a.size(); i++) {
				bool matched = false;
				for (const auto& k : known) {
					if (a[i] != k.first)
						continue;
					if (i + std::size_t(k.second) >= a.size())
						throw UsageError(k.first + " takes " + std::to_string(k.second) +
						                 " value" + (k.second == 1 ? "" : "s"));
					out.given.push_back({k.first, std::vector<std::string>(
					                                a.begin() + long(i) + 1,
					                                a.begin() + long(i) + 1 + k.second)});
					i += std::size_t(k.second);
					matched = true;
					break;
				}
				if (!matched)
					out.rest.push_back(a[i]);
			}
			return out;
		}

		// --- Edits ---------------------------------------------------------

		// One edit of `scene` (`a` is the command and its arguments, without
		// the file). Returns false when `a` is no edit. Throws on a bad edit.
		bool Edit(Scene& scene, const std::vector<std::string>& a) {
			const std::string& cmd = a[0];

			if (cmd == "add-model") {
				const Options o = TakeOptions(a, 1, {{"--name", 1}, {"--at", 3}});
				Need(o.rest, 2, "add-model PARENT MODEL [--name NAME] [--at X Y Z]");
				VoxelObject* holder;
				Scene& siblings = ChildrenOf(scene, o.rest[0], &holder);
				const std::string& file = o.rest[1];
				std::string base = LocalFileSystem::GetFileName(file);
				const std::size_t dot = base.rfind('.');
				if (dot != std::string::npos && dot > 0)
					base.resize(dot);
				if (o.Has("--name")) {
					base = o.Values("--name")[0];
					CheckName(base);
				}
				const Vector3 at = o.Has("--at") ? ParseVector(o.Values("--at"), 0, "--at")
				                                 : MakeVector3(0.0F, 0.0F, 0.0F);
				if (IsScenePath(file)) {
					// A scene's objects come in as they stand, under a hinge
					// named after it, which --at places.
					VoxelObject group;
					group.name = UnusedName(siblings, base);
					group.position = at;
					group.children = LoadScene(file);
					siblings.push_back(std::move(group));
				} else {
					VoxelObject object;
					object.name = UnusedName(siblings, base);
					object.model = LoadModel(file);
					object.position = at;
					siblings.push_back(std::move(object));
				}
				return true;
			}
			if (cmd == "add-hinge") {
				const Options o = TakeOptions(a, 1, {{"--at", 3}, {"--lock", 0}});
				Need(o.rest, 2, "add-hinge PARENT NAME [--at X Y Z] [--lock]");
				CheckName(o.rest[1]);
				VoxelObject* holder;
				Scene& siblings = ChildrenOf(scene, o.rest[0], &holder);
				VoxelObject hinge;
				hinge.name = UnusedName(siblings, o.rest[1]);
				if (o.Has("--at"))
					hinge.position = ParseVector(o.Values("--at"), 0, "--at");
				hinge.locked = o.Has("--lock");
				siblings.push_back(std::move(hinge));
				return true;
			}
			if (cmd == "set-model") {
				Need(a, 3, "set-model OBJECT MODEL.kv6");
				// A template's locked hinge is meant to be filled in.
				Find(scene, a[1]).Object().model = LoadModel(a[2]);
				return true;
			}
			if (cmd == "clear-model") {
				Need(a, 2, "clear-model OBJECT");
				Find(scene, a[1]).Object().model = Handle<VoxelModel>();
				return true;
			}
			if (cmd == "move" || cmd == "set-position") {
				Need(a, 5, (cmd + " OBJECT X Y Z").c_str());
				VoxelObject& o = Find(scene, a[1]).Object();
				const Vector3 v = ParseVector(a, 2, "a position");
				o.position = cmd == "move" ? o.position + v : v;
				return true;
			}
			if (cmd == "rotate" || cmd == "set-rotation") {
				if (cmd == "rotate")
					Need(a, 4, "rotate OBJECT AXIS DEGREES");
				else
					Need(a, 5, "set-rotation OBJECT RX RY RZ");
				VoxelObject& o = Find(scene, a[1]).Object();
				RefuseLocked(o, "turn");
				if (cmd == "rotate") {
					// In its parent's axes, about its own origin.
					o.rotation =
					  Compose(AxisAngle(ParseAxis(a[2]), ParseFloat(a[3], "the degrees")), o.rotation);
				} else {
					o.rotation = FromEuler(ParseFloat(a[2], "RX"), ParseFloat(a[3], "RY"),
					                       ParseFloat(a[4], "RZ"));
				}
				return true;
			}
			if (cmd == "scale" || cmd == "set-scale") {
				if (a.size() != 3 && a.size() != 5)
					throw UsageError("usage: " + cmd + " OBJECT F | FX FY FZ");
				VoxelObject& o = Find(scene, a[1]).Object();
				RefuseLocked(o, "scale");
				const Vector3 f = a.size() == 3 ? MakeVector3(1, 1, 1) * ParseFloat(a[2], "the scale")
				                                : ParseVector(a, 2, "the scale");
				if (f.x == 0.0F || f.y == 0.0F || f.z == 0.0F)
					throw UsageError("a scale of 0 flattens the object away");
				o.scale = cmd == "scale" ? MakeVector3(o.scale.x * f.x, o.scale.y * f.y, o.scale.z * f.z)
				                         : f;
				return true;
			}
			if (cmd == "rename") {
				Need(a, 3, "rename OBJECT NAME");
				CheckName(a[2]);
				Ref ref = Find(scene, a[1]);
				RefuseLocked(ref.Object(), "rename");
				ref.Object().name = UnusedName(*ref.siblings, a[2], &ref.Object());
				return true;
			}
			if (cmd == "remove") {
				const Options o = TakeOptions(a, 1, {{"--force", 0}});
				Need(o.rest, 1, "remove OBJECT [--force]");
				Ref ref = Find(scene, o.rest[0]);
				if (!o.Has("--force") && AnyLocked(ref.Object()))
					throw std::runtime_error("a locked object would go with it (--force)");
				ref.siblings->erase(ref.siblings->begin() + long(ref.index));
				return true;
			}
			if (cmd == "reparent") {
				const Options o = TakeOptions(a, 1, {{"--keep-local", 0}});
				Need(o.rest, 2, "reparent OBJECT PARENT [--keep-local]");
				Ref ref = Find(scene, o.rest[0]);
				RefuseLocked(ref.Object(), "move under another object");
				VoxelObject* holder;
				ChildrenOf(scene, o.rest[1], &holder);
				if (holder && Contains(ref.Object(), holder))
					throw std::runtime_error("an object cannot go under itself or what is under it");
				const Matrix4 world = ParentTransform(scene, &ref.Object()) * LocalTransform(ref.Object());
				VoxelObject moving = std::move(ref.Object());
				ref.siblings->erase(ref.siblings->begin() + long(ref.index));
				// Found again: taking the object out may have moved its new parent.
				Scene& siblings = ChildrenOf(scene, o.rest[1], &holder);
				if (!o.Has("--keep-local")) {
					const Matrix4 above = holder ? ParentTransform(scene, holder) * LocalTransform(*holder)
					                             : Matrix4::Identity();
					Decompose(above.Inversed() * world, moving);
				}
				moving.name = UnusedName(siblings, moving.name);
				siblings.push_back(std::move(moving));
				return true;
			}
			if (cmd == "lock" || cmd == "unlock" || cmd == "hide" || cmd == "show") {
				Need(a, 2, (cmd + " OBJECT").c_str());
				VoxelObject& o = Find(scene, a[1]).Object();
				if (cmd == "lock" || cmd == "unlock")
					o.locked = cmd == "lock";
				else
					o.hidden = cmd == "hide";
				return true;
			}
			return false;
		}

		// --- Reading -------------------------------------------------------

		void PrintTree(Scene& scene, bool world, bool json) {
			if (json)
				std::printf("[");
			int n = 0;
			bool first = true;
			std::vector<std::string> path;
			std::function<void(Scene&, const Matrix4&, int)> print = [&](Scene& level,
			                                                            const Matrix4& above,
			                                                            int depth) {
				for (VoxelObject& o : level) {
					n++;
					path.resize(std::size_t(depth));
					path.push_back(o.name);
					std::string full;
					for (const std::string& p : path)
						full += (full.empty() ? "" : "/") + p;
					VoxelObject shown = o;
					const Matrix4 mine = above * LocalTransform(o);
					if (world)
						Decompose(mine, shown);
					const Vector3 turn = ToEuler(shown.rotation);
					std::string kind = "hinge";
					if (o.model)
						kind = "model " + std::to_string(o.model->GetWidth()) + "x" +
						       std::to_string(o.model->GetHeight()) + "x" +
						       std::to_string(o.model->GetDepth());
					if (json) {
						std::printf("%s{\"number\": %d, \"path\": \"%s\", \"depth\": %d, \"kind\": "
						            "\"%s\", \"position\": [%s], \"rotation\": [%s], \"scale\": "
						            "[%s], \"locked\": %s, \"hidden\": %s, \"children\": %d}",
						            first ? "" : ", ", n, full.c_str(), depth,
						            o.model ? "model" : "hinge", Format3(shown.position, ", ").c_str(),
						            Format3(turn, ", ").c_str(), Format3(shown.scale, ", ").c_str(),
						            o.locked ? "true" : "false", o.hidden ? "true" : "false",
						            int(o.children.size()));
						first = false;
					} else {
						std::printf("#%-3d %s%s  [%s]  at %s  turn %s  scale %s%s%s\n", n,
						            std::string(std::size_t(depth) * 2, ' ').c_str(),
						            o.name.empty() ? "(unnamed)" : o.name.c_str(), kind.c_str(),
						            Format3(shown.position).c_str(), Format3(turn).c_str(),
						            Format3(shown.scale).c_str(), o.locked ? "  locked" : "",
						            o.hidden ? "  hidden" : "");
					}
					print(o.children, mine, depth + 1);
				}
			};
			print(scene, Matrix4::Identity(), 0);
			if (json)
				std::printf("]\n");
			else if (n == 0)
				std::printf("(no objects)\n");
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
			std::vector<std::string> rest{cmd};
			rest.insert(rest.end(), args.begin() + 2, args.end());

			if (cmd == "new") {
				const Options o = TakeOptions(rest, 1, {{"--force", 0}});
				Need(o.rest, 0, "new FILE [--force]");
				if (!o.Has("--force") && LocalFileSystem::Exists(path))
					throw std::runtime_error(path + " exists already (--force to replace it)");
				SaveScene(Scene(), path);
				return 0;
			}

			Scene scene = LoadScene(path);

			if (cmd == "tree") {
				const Options o = TakeOptions(rest, 1, {{"--world", 0}, {"--json", 0}});
				Need(o.rest, 0, "tree FILE [--world] [--json]");
				PrintTree(scene, o.Has("--world"), o.Has("--json"));
				return 0;
			}
			if (cmd == "info") {
				Need(rest, 1, "info FILE");
				int objects = 0, models = 0, locked = 0, hidden = 0;
				Walk(scene, nullptr, [&](const Ref& ref, int) {
					objects++;
					models += ref.Object().model ? 1 : 0;
					locked += ref.Object().locked ? 1 : 0;
					hidden += ref.Object().hidden ? 1 : 0;
				});
				std::printf("objects %d\nmodels %d\nhinges %d\nlocked %d\nhidden %d\n", objects,
				            models, objects - models, locked, hidden);
				try {
					IntVector3 low;
					Handle<VoxelModel> m = VoxelModelLoader::MergeScene(scene, &low);
					std::printf("composed %d %d %d from %d %d %d\n", m->GetWidth(), m->GetHeight(),
					            m->GetDepth(), low.x, low.y, low.z);
				} catch (const std::exception&) {
					std::printf("composed empty\n");
				}
				return 0;
			}
			if (cmd == "extract" || cmd == "flatten") {
				Handle<VoxelModel> model;
				if (cmd == "flatten") {
					Need(rest, 2, "flatten FILE OUT.kv6");
					model = Composed(scene);
					if (!gui::KV6ModelIO::Save(*model, rest[1]))
						throw std::runtime_error("cannot write " + rest[1]);
					return 0;
				}
				const Options o = TakeOptions(rest, 1, {{"--self", 0}});
				Need(o.rest, 2, "extract FILE OBJECT OUT [--self]");
				Ref ref = Find(scene, o.rest[0]);
				const std::string& out = o.rest[1];
				VoxelObject object = ref.Object();
				if (IsScenePath(out)) {
					// Where it stands in the scene, with all under it.
					Decompose(ParentTransform(scene, &ref.Object()) * LocalTransform(ref.Object()),
					          object);
					SaveScene(Scene{object}, out);
					return 0;
				}
				if (o.Has("--self") || object.children.empty()) {
					if (!object.model)
						throw std::runtime_error("'" + object.name + "' is a hinge: it holds no voxels");
					model = object.model;
				} else {
					// In its own frame: composed with all under it.
					object.position = MakeVector3(0, 0, 0);
					object.rotation = MakeVector4(0, 0, 0, 1);
					object.scale = MakeVector3(1, 1, 1);
					object.hidden = false;
					model = Composed(Scene{object});
				}
				if (!gui::KV6ModelIO::Save(*model, out))
					throw std::runtime_error("cannot write " + out);
				return 0;
			}
			if (cmd == "render") {
				const Options o = TakeOptions(rest, 1, {{"--object", 1}});
				Scene shown = scene;
				if (o.Has("--object")) {
					// The object where it stands, with all under it.
					Ref ref = Find(scene, o.Values("--object")[0]);
					VoxelObject object = ref.Object();
					object.hidden = false;
					Decompose(ParentTransform(scene, &ref.Object()) * LocalTransform(ref.Object()),
					          object);
					shown = Scene{object};
				}
				Handle<VoxelModel> model = Composed(shown);
				std::vector<std::string> request{"render"};
				request.insert(request.end(), o.rest.begin(), o.rest.end());
				return RenderKV6Model(*model, request);
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
						if (!Edit(scene, words))
							throw UsageError("not an edit: '" + words[0] + "'");
					} catch (const std::exception& ex) {
						// Nothing is saved: a batch applies whole or not at all.
						throw std::runtime_error("line " + std::to_string(number) + ": " + ex.what());
					}
				}
				SaveScene(scene, path);
				return 0;
			}
			if (!Edit(scene, rest))
				throw UsageError("unknown command '" + cmd + "' (try help)");
			SaveScene(scene, path);
			return 0;
		}
	} // namespace

	int RunTwoKV6Tools(int argc, char** argv) {
		std::vector<std::string> args;
		for (int i = 0; i < argc; i++)
			args.push_back(argv[i] ? argv[i] : "");
		try {
			return Run(args);
		} catch (const UsageError& ex) {
			std::fprintf(stderr, "2kv6-tools: %s\n", ErrorText(ex).c_str());
			return 2;
		} catch (const std::exception& ex) {
			std::fprintf(stderr, "2kv6-tools: %s\n", ErrorText(ex).c_str());
			return 1;
		}
	}
} // namespace spades
