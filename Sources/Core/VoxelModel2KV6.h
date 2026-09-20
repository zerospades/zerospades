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

		// Animation keyframes (if empty, object is static)
		std::vector<TransformKeyframe> keyframes;

		// Scene hierarchy: child objects (empty for leaf nodes)
		std::vector<VoxelObject> children;

		VoxelObject() : position(0, 0, 0), rotation(0, 0, 0, 1), scale(1, 1, 1) {}
	};

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
	 *   4       uint16    Version (current: 1)
	 *   6       uint16    NumRootObjects
	 *
	 * OBJECT (recursive structure):
	 *   NameLength: uint16
	 *   Name: char[NameLength]
	 *   Position: float[3] (local transform)
	 *   Rotation: float[4] (quaternion x,y,z,w; local transform)
	 *   Scale: float[3] (local transform)
	 *   HasModel: uint8 (0=group node, 1=has KV6 voxel data)
	 *   [If HasModel==1]: Embedded KV6 data (no magic, just header+blocks+indices)
	 *   NumKeyframes: uint16
	 *   [Keyframes]: Time float, Position float[3], Rotation float[4], Scale float[3]
	 *   NumChildren: uint16
	 *   [Recursively]: child objects
	 */
	class VoxelModel2KV6 {
	public:
		VoxelModel2KV6() = delete;
		VoxelModel2KV6(const VoxelModel2KV6&) = delete;
		void operator=(const VoxelModel2KV6&) = delete;

		static constexpr uint16_t FORMAT_VERSION = 1;

		/**
		 * Load a .2kv6 scene from a stream.
		 * Returns a vector of VoxelObject, one per object in the scene.
		 */
		static std::vector<VoxelObject> Load(IStream& stream);

		/**
		 * Save a .2kv6 scene to a stream.
		 * Objects should contain valid models with optional animation keyframes.
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
