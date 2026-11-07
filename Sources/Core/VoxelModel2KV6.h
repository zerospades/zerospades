/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades.

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

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include "Math.h"
#include "VoxelModel.h"

namespace spades {
	/**
	 * .2kv6: a scene of named KV6 models, each placed (position, rotation,
	 * scale) relative to its parent, forming a tree, with the scene's own
	 * named notes and named animations. No bones: an animation moves objects.
	 */

	/** Where an object is at one moment of an animation: its local transform. */
	struct TransformKeyframe {
		float time; ///< seconds from the start of the clip
		Vector3 position;
		Vector4 rotation; ///< quaternion (x, y, z, w)
		Vector3 scale;
	};

	/** The 8 bytes every .2kv6 starts with. */
	struct KV6FileHeader {
		char magic[4]; // "2kv6"
		uint16_t version;
		uint16_t numRootObjects;
	};

	/**
	 * A named note of the scene: UTF-8 text, nothing else. Only the scene
	 * carries them, never one of its objects.
	 */
	struct MetadataEntry {
		std::string name; ///< unique in the scene, 1 to 255 bytes
		std::string text; ///< UTF-8, no NUL
	};

	/** The keys of one object in a clip, the object named by its path ("Gun/Bolt"). */
	struct AnimationTrack {
		std::string path;
		std::vector<TransformKeyframe> keyframes; ///< times within the clip's duration
	};

	/**
	 * A named animation of the whole scene: its tracks move any of its
	 * objects. A template locks what a scene built from it must keep: its
	 * name (scripts play it by name) and its duration (they time it), which
	 * an editor then refuses to change. Stored only: nothing plays them yet.
	 */
	struct AnimationClip {
		std::string name; ///< unique in the scene, 1 to 255 bytes
		float duration = 1.0F; ///< seconds, above zero
		bool nameLocked = false;
		bool durationLocked = false;
		std::vector<AnimationTrack> tracks;
	};

	/**
	 * A named object of a .2kv6 scene: a KV6 model (or none: a hinge, which
	 * only groups and places what hangs under it), its transform relative to
	 * its parent, and its children. Its world transform composes those up
	 * the tree. Its animation, if any, lives in the scene's clips.
	 */
	struct VoxelObject {
		std::string name; ///< unique among its siblings
		Handle<VoxelModel> model; ///< null for a hinge

		Vector3 position;
		Vector4 rotation; ///< Quaternion (x, y, z, w); identity = (0, 0, 0, 1)
		Vector3 scale;

		/**
		 * Held in place by a template: an editor keeps it from being renamed,
		 * deleted, moved under another object, turned or scaled, and lets it be
		 * unlocked only on purpose: a template's hinges are locked.
		 */
		bool locked = false;

		/**
		 * Not drawn, and neither is anything under it: kept in the file, but
		 * out of sight wherever the scene is shown.
		 */
		bool hidden = false;

		std::vector<VoxelObject> children;

		VoxelObject() : position(0, 0, 0), rotation(0, 0, 0, 1), scale(1, 1, 1) {}
	};

	/** What a local position, rotation and scale do to a point: scale it, turn
	 *  it, then move it, as an object's transform does to its voxels. */
	Matrix4 LocalTransform(const Vector3& position, const Vector4& rotation, const Vector3& scale);

	/**
	 * `m` back into a position, a rotation and a scale, LocalTransform's
	 * inverse. False when they cannot hold it exactly: it shears (an object
	 * turned within a parent scaled unevenly), or flattens an axis; they are
	 * then the nearest that leave the shear out.
	 */
	bool DecomposeTransform(const Matrix4& m, Vector3& position, Vector4& rotation,
	                        Vector3& scale);

	/** The rotation of turns about X, then Y, then Z, each about the fixed
	 *  axes, in degrees: as --2kv6-tools and the editors set and show turns. */
	Vector4 RotationFromEuler(const Vector3& degrees);
	/** The turns RotationFromEuler takes to make `rotation`; looking straight
	 *  along Y, X takes all of the turn about Z. */
	Vector3 EulerFromRotation(const Vector4& rotation);

	/**
	 * Reads and writes .2kv6 files.
	 *
	 * FILE FORMAT, version 2
	 * ======================
	 * All integers are little-endian, all floats IEEE 754 single precision,
	 * all text UTF-8. A file is, in this order:
	 *
	 *   HEADER
	 *     char[4]  Magic             "2kv6"
	 *     uint16   Version           2
	 *     uint16   NumRootObjects
	 *   OBJECT   x NumRootObjects
	 *   METADATA (the scene's)
	 *   CLIPS    (the scene's)
	 *
	 * OBJECT, depth first, 64 levels at most:
	 *     uint16   NameLength, then char[NameLength] Name
	 *     float[3] Position          relative to the parent
	 *     float[4] Rotation          quaternion x, y, z, w
	 *     float[3] Scale
	 *     uint8    Flags             bit 0 locked, bit 1 hidden, others zero
	 *     uint8    HasModel          0 a hinge, 1 a model follows
	 *     [KV6]    if HasModel: a KV6 without its magic (header, blocks, indices)
	 *     uint16   NumChildren, then OBJECT x NumChildren
	 *
	 * METADATA: named text, the scene's only:
	 *     uint16   Count, then each entry:
	 *     uint16   NameLength, then char[NameLength] Name   1 to 255 bytes, unique
	 *     uint32   TextLength, then char[TextLength] Text   no NUL, 16 MiB at most
	 *
	 * CLIPS: named animations of the scene:
	 *     uint16   Count, then each clip:
	 *     uint16   NameLength, then char[NameLength] Name   1 to 255 bytes, unique
	 *     float    Duration          seconds, above zero
	 *     uint8    Flags             bit 0 name locked, bit 1 duration locked,
	 *                                others zero
	 *     uint16   NumTracks, then each track:
	 *       uint16 PathLength, then char[PathLength] Path   "Gun/Bolt": names
	 *                                from the top, joined by '/'
	 *       uint16 NumKeys, then each key:
	 *         float    Time          seconds, 0 to Duration
	 *         float[3] Position, float[4] Rotation, float[3] Scale
	 *
	 * Version 1, still read (and no longer written), is the same up to
	 * METADATA and CLIPS, which it has not, and gives every OBJECT a uint16
	 * NumKeys and its keys (as above) after its KV6; files of it carry none,
	 * and one that does is refused: animations are the scene's clips.
	 */
	class VoxelModel2KV6 {
	public:
		VoxelModel2KV6() = delete;
		VoxelModel2KV6(const VoxelModel2KV6&) = delete;
		void operator=(const VoxelModel2KV6&) = delete;

		/** The newest version read and written. */
		static constexpr uint16_t FORMAT_VERSION = 2;

		/** A whole .2kv6 file: its objects, its notes and its animations. */
		struct SceneFile {
			std::vector<VoxelObject> objects;
			std::vector<MetadataEntry> metadata;
			std::vector<AnimationClip> clips;
		};

		/** Reads a whole .2kv6 file, of either version. */
		static SceneFile LoadScene(IStream& stream);
		/** Writes a whole .2kv6 file, as FORMAT_VERSION; checks every
		 *  metadata entry and clip first (Validate). */
		static void SaveScene(IStream& stream, const SceneFile& scene);

		/** Throws, saying why, when `entry` has no name or is not UTF-8 text. */
		static void Validate(const MetadataEntry& entry);
		/** Throws when `clip` has no name, no duration, or a key past its end. */
		static void Validate(const AnimationClip& clip);

		/** The root objects of a .2kv6 scene, its metadata and clips left out. */
		static std::vector<VoxelObject> Load(IStream& stream);

		/** A .2kv6 scene of these root objects alone, with no metadata or clips. */
		static void Save(IStream& stream, const std::vector<VoxelObject>& objects);

		// Scene editing utilities
		static VoxelObject CreateObject(const std::string& name, int sizeXYZ);
		static bool DeleteObject(VoxelObject& parent, size_t childIndex);
		static VoxelObject* FindObjectByName(std::vector<VoxelObject>& scene,
		                                      const std::string& name);

	private:
		static constexpr const char* MAGIC = "2kv6";
	};

} // namespace spades
