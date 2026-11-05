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
	 * .2kv6 format: Next-generation KV6 with support for multiple named objects,
	 * each with their own transform (position, rotation, scale) and optional
	 * animation keyframes. This enables:
	 * - Scene graphs of named, transformed KV6 models
	 * - Per-object transform animation (no bones/skeleton)
	 * - Compatibility with single-object KV6 models
	 */

	/**
	 * Represents a single keyframe of transform animation.
	 * Animates position, rotation (quaternion), and scale independently.
	 */
	/**
	 * Single keyframe of transform animation.
	 * Animates position, rotation (quaternion), and scale independently.
	 */
	struct TransformKeyframe {
		float time; ///< Keyframe time in seconds
		Vector3 position;
		Vector4 rotation; ///< Quaternion (x, y, z, w)
		Vector3 scale;
	};

	/**
	 * .2kv6 file header (8 bytes).
	 * Must appear at offset 0 of every .2kv6 file.
	 */
	struct KV6FileHeader {
		char magic[4]; // "2kv6" (bytes 0-3)
		uint16_t version; // Current: 1 (bytes 4-5)
		uint16_t numRootObjects; // Number of root objects in scene (bytes 6-7)
	};

	/**
	 * A named piece of data a scene or one of its objects carries, of one of
	 * a few types, each checked on reading and writing: no raw binary ever
	 * travels in a .2kv6.
	 */
	enum class MetadataType : uint8_t {
		Text = 1,   ///< UTF-8 text, no NUL
		Audio = 2,  ///< an Opus stream in Ogg ("OggS", then "OpusHead")
		Image = 3,  ///< a PNG image (its 8-byte signature)
		Script = 4, ///< AngelScript source: UTF-8 text, no NUL; stored, never run here
	};

	struct MetadataEntry {
		std::string name; ///< unique within its table, 1 to 255 bytes
		MetadataType type = MetadataType::Text;
		std::string data; ///< the bytes, as the type says
	};

	/** A keyframed track of one object of a clip, named by its path ("Gun/Bolt"). */
	struct AnimationTrack {
		std::string path;
		std::vector<TransformKeyframe> keyframes; ///< times within the clip's duration
	};

	/**
	 * A named animation of a scene. A template locks what a scene built from
	 * it must keep: its name (scripts play it by name) and its duration (they
	 * time it), which an editor then refuses to change.
	 */
	struct AnimationClip {
		std::string name; ///< unique in the scene, 1 to 255 bytes
		float duration = 1.0F; ///< seconds, above zero
		bool nameLocked = false;
		bool durationLocked = false;
		std::vector<AnimationTrack> tracks;
	};

	/**
	 * Represents a named voxel object within a .2kv6 scene.
	 * Each object contains a KV6 voxel model with optional transform animation
	 * and can have child objects forming a scene hierarchy.
	 *
	 * Transforms are LOCAL (relative to parent). World transform is computed by
	 * composing transforms up the hierarchy.
	 */
	struct VoxelObject {
		std::string name; ///< Object name (empty for unnamed objects)
		Handle<VoxelModel> model; ///< The KV6 voxel model data (can be null for group nodes)

		// Local transform (used when there are no keyframes)
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

		// Animation keyframes (if empty, object is static)
		std::vector<TransformKeyframe> keyframes;

		// Scene hierarchy: child objects (empty for leaf nodes)
		std::vector<VoxelObject> children;

		/** Named data this object carries (a sound of it, a script driving it). */
		std::vector<MetadataEntry> metadata;

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
	 * .2kv6 file format handler for hierarchical scenes of named voxel objects.
	 *
	 * Supports nested objects forming a scene graph tree. Transforms are LOCAL
	 * (relative to parent); world transforms are computed by composing up the hierarchy.
	 *
	 * FILE FORMAT SPECIFICATION:
	 * ===========================
	 * All multi-byte integers are little-endian.
	 *
	 * HEADER (8 bytes total):
	 *   Offset  Type      Field
	 *   0       char[4]   Magic signature ("2kv6")
	 *   4       uint16    Version (1 or 2)
	 *   6       uint16    NumRootObjects
	 *
	 * Version 2 adds named, typed metadata and named animation clips; a scene
	 * using none of them is still written as version 1, which version 2
	 * readers read too.
	 *
	 * OBJECT (recursive structure):
	 *   NameLength: uint16
	 *   Name: char[NameLength]
	 *   Position: float[3] (local transform)
	 *   Rotation: float[4] (quaternion x,y,z,w; local transform)
	 *   Scale: float[3] (local transform)
	 *   Flags: uint8 (bit 0: locked; bit 1: hidden; the other bits are zero)
	 *   HasModel: uint8 (0=group node, 1=has KV6 voxel data)
	 *   [If HasModel==1]: Embedded KV6 data (no magic, just header+blocks+indices)
	 *   NumKeyframes: uint16
	 *   [Keyframes]: Time float, Position float[3], Rotation float[4], Scale float[3]
	 *   [Version 2]: METADATA TABLE (the object's)
	 *   NumChildren: uint16
	 *   [Recursively]: child objects
	 *
	 * [Version 2] after the root objects:
	 *   METADATA TABLE (the scene's)
	 *   NumClips: uint16, then each CLIP
	 *
	 * METADATA TABLE: Count uint16, then each entry:
	 *   NameLength uint16, Name char[NameLength]
	 *   Type uint8 (1 text, 2 audio, 3 image, 4 script)
	 *   DataLength uint32, Data byte[DataLength]
	 *
	 * CLIP:
	 *   NameLength uint16, Name char[NameLength]
	 *   Duration float (seconds)
	 *   Flags uint8 (bit 0: name locked; bit 1: duration locked; others zero)
	 *   NumTracks uint16, then each track:
	 *     PathLength uint16, Path char[PathLength]
	 *     NumKeyframes uint16, then keyframes as above
	 */
	class VoxelModel2KV6 {
	public:
		VoxelModel2KV6() = delete;
		VoxelModel2KV6(const VoxelModel2KV6&) = delete;
		void operator=(const VoxelModel2KV6&) = delete;

		/** The newest version read and written. */
		static constexpr uint16_t FORMAT_VERSION = 2;

		/** A whole .2kv6 file: its objects, and what version 2 adds around them. */
		struct SceneFile {
			std::vector<VoxelObject> objects;
			std::vector<MetadataEntry> metadata; ///< the scene's own
			std::vector<AnimationClip> clips;
		};

		/** Reads a whole .2kv6 file, of either version. */
		static SceneFile LoadScene(IStream& stream);
		/** Writes a whole .2kv6 file, as version 1 when it uses nothing of
		 *  version 2; checks every metadata entry and clip first (Validate). */
		static void SaveScene(IStream& stream, const SceneFile& scene);

		/** Throws, saying why, when `entry` is not what its type promises. */
		static void Validate(const MetadataEntry& entry);
		/** Throws when `clip` has no name, no duration, or a key past its end. */
		static void Validate(const AnimationClip& clip);

		/**
		 * Load a .2kv6 scene from a stream.
		 * Returns a vector of VoxelObject, one per object in the scene. A
		 * scene's own metadata and clips are left out: LoadScene keeps them.
		 */
		static std::vector<VoxelObject> Load(IStream& stream);

		/**
		 * Save a .2kv6 scene to a stream.
		 * Objects should contain valid models with optional animation keyframes.
		 * A scene's own metadata and clips are SaveScene's.
		 */
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
