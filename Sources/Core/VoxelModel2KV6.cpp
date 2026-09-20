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

#include "VoxelModel2KV6.h"

#include <cstring>
#include <algorithm>

#include "Debug.h"
#include "Exception.h"
#include "IStream.h"

namespace spades {
	namespace {
		VoxelObject LoadObject(IStream& stream, int depth = 0) {
			if (depth > 64)
				SPRaise("Object hierarchy too deep (max 64 levels)");

			VoxelObject obj;

			// Read object name
			uint16_t nameLen;
			if (stream.Read(&nameLen, sizeof(nameLen)) < sizeof(nameLen))
				SPRaise(".2kv6 file truncated: failed to read name length at depth %d", depth);

			if (nameLen > 0) {
				std::string name(nameLen, '\0');
				if (stream.Read(&name[0], nameLen) < nameLen)
					SPRaise(".2kv6 file truncated: failed to read name at depth %d", depth);
				obj.name = name;
			}

			// Read local transform
			float posData[3], rotData[4], scaleData[3];

			if (stream.Read(posData, sizeof(posData)) < sizeof(posData))
				SPRaise(".2kv6 file truncated: failed to read position at depth %d", depth);
			obj.position = MakeVector3(posData[0], posData[1], posData[2]);

			if (stream.Read(rotData, sizeof(rotData)) < sizeof(rotData))
				SPRaise(".2kv6 file truncated: failed to read rotation at depth %d", depth);
			obj.rotation = MakeVector4(rotData[0], rotData[1], rotData[2], rotData[3]);

			if (stream.Read(scaleData, sizeof(scaleData)) < sizeof(scaleData))
				SPRaise(".2kv6 file truncated: failed to read scale at depth %d", depth);
			obj.scale = MakeVector3(scaleData[0], scaleData[1], scaleData[2]);

			// Read hasModel flag
			uint8_t hasModel;
			if (stream.Read(&hasModel, sizeof(hasModel)) < sizeof(hasModel))
				SPRaise(".2kv6 file truncated: failed to read hasModel flag at depth %d", depth);

			// Load KV6 data if present
			if (hasModel != 0) {
				try {
					obj.model = VoxelModel::LoadKV6(stream);
				} catch (const std::exception& e) {
					SPRaise("Failed to load KV6 data at depth %d: %s", depth, e.what());
				}
			}

			// Read animation keyframes
			uint16_t numKeyframes;
			if (stream.Read(&numKeyframes, sizeof(numKeyframes)) < sizeof(numKeyframes))
				SPRaise(".2kv6 file truncated: failed to read keyframe count at depth %d", depth);

			obj.keyframes.reserve(numKeyframes);
			for (uint16_t k = 0; k < numKeyframes; k++) {
				TransformKeyframe kf;

				if (stream.Read(&kf.time, sizeof(kf.time)) < sizeof(kf.time))
					SPRaise(".2kv6 file truncated: failed to read keyframe time at depth %d "
					        "keyframe %u",
					        depth, k);

				float kfPosData[3], kfRotData[4], kfScaleData[3];

				if (stream.Read(kfPosData, sizeof(kfPosData)) < sizeof(kfPosData))
					SPRaise(".2kv6 file truncated: failed to read keyframe position at depth %d "
					        "keyframe %u",
					        depth, k);
				kf.position = MakeVector3(kfPosData[0], kfPosData[1], kfPosData[2]);

				if (stream.Read(kfRotData, sizeof(kfRotData)) < sizeof(kfRotData))
					SPRaise(".2kv6 file truncated: failed to read keyframe rotation at depth %d "
					        "keyframe %u",
					        depth, k);
				kf.rotation = MakeVector4(kfRotData[0], kfRotData[1], kfRotData[2], kfRotData[3]);

				if (stream.Read(kfScaleData, sizeof(kfScaleData)) < sizeof(kfScaleData))
					SPRaise(".2kv6 file truncated: failed to read keyframe scale at depth %d "
					        "keyframe %u",
					        depth, k);
				kf.scale = MakeVector3(kfScaleData[0], kfScaleData[1], kfScaleData[2]);

				obj.keyframes.push_back(kf);
			}

			// Read child objects
			uint16_t numChildren;
			if (stream.Read(&numChildren, sizeof(numChildren)) < sizeof(numChildren))
				SPRaise(".2kv6 file truncated: failed to read child count at depth %d", depth);

			obj.children.reserve(numChildren);
			for (uint16_t c = 0; c < numChildren; c++) {
				obj.children.push_back(LoadObject(stream, depth + 1));
			}

			return obj;
		}

		void SaveObject(IStream& stream, const VoxelObject& obj, int depth = 0) {
			if (depth > 64)
				SPRaise("Object hierarchy too deep (max 64 levels)");

			// Write object name
			size_t nameLength = obj.name.length();
			if (nameLength > UINT16_MAX)
				SPRaise("Object name too long: %zu characters", nameLength);
			uint16_t nameLen = static_cast<uint16_t>(nameLength);

			stream.Write(&nameLen, sizeof(nameLen));
			if (nameLen > 0)
				stream.Write(obj.name.data(), nameLen);

			// Write local transform
			float posData[3] = {obj.position.x, obj.position.y, obj.position.z};
			float rotData[4] = {obj.rotation.x, obj.rotation.y, obj.rotation.z, obj.rotation.w};
			float scaleData[3] = {obj.scale.x, obj.scale.y, obj.scale.z};

			stream.Write(posData, sizeof(posData));
			stream.Write(rotData, sizeof(rotData));
			stream.Write(scaleData, sizeof(scaleData));

			// Write hasModel flag
			uint8_t hasModel = obj.model ? 1 : 0;
			stream.Write(&hasModel, sizeof(hasModel));

			// Write KV6 data if present
			if (obj.model) {
				obj.model->SaveKV6(stream);
			}

			// Write animation keyframes
			uint16_t numKeyframes = static_cast<uint16_t>(obj.keyframes.size());
			stream.Write(&numKeyframes, sizeof(numKeyframes));

			for (const TransformKeyframe& kf : obj.keyframes) {
				stream.Write(&kf.time, sizeof(kf.time));

				float kfPosData[3] = {kf.position.x, kf.position.y, kf.position.z};
				float kfRotData[4] = {kf.rotation.x, kf.rotation.y, kf.rotation.z, kf.rotation.w};
				float kfScaleData[3] = {kf.scale.x, kf.scale.y, kf.scale.z};

				stream.Write(kfPosData, sizeof(kfPosData));
				stream.Write(kfRotData, sizeof(kfRotData));
				stream.Write(kfScaleData, sizeof(kfScaleData));
			}

			// Write child objects
			uint16_t numChildren = static_cast<uint16_t>(obj.children.size());
			if (numChildren > UINT16_MAX)
				SPRaise("Too many children for object '%s': %zu (max %u)", obj.name.c_str(),
				        obj.children.size(), UINT16_MAX);

			stream.Write(&numChildren, sizeof(numChildren));

			for (const VoxelObject& child : obj.children) {
				SaveObject(stream, child, depth + 1);
			}
		}
	} // namespace

	std::vector<VoxelObject> VoxelModel2KV6::Load(IStream& stream) {
		SPADES_MARK_FUNCTION();

		// Read and verify file header (8 bytes total)
		KV6FileHeader header;
		if (stream.Read(&header, sizeof(header)) < sizeof(header))
			SPRaise(".2kv6 file truncated: failed to read header (need 8 bytes)");

		// Verify magic signature
		if (std::strncmp(header.magic, MAGIC, 4) != 0)
			SPRaise("Invalid .2kv6 file: magic signature mismatch (not a .2kv6 file)");

		// Check version compatibility
		if (header.version != FORMAT_VERSION)
			SPRaise("Unsupported .2kv6 format version: %u (this reader supports version %u)",
			        header.version, FORMAT_VERSION);

		uint16_t numRootObjects = header.numRootObjects;

		std::vector<VoxelObject> objects;
		objects.reserve(numRootObjects);

		for (uint16_t i = 0; i < numRootObjects; i++) {
			objects.push_back(LoadObject(stream, 0));
		}

		return objects;
	}

	void VoxelModel2KV6::Save(IStream& stream, const std::vector<VoxelObject>& objects) {
		SPADES_MARK_FUNCTION();

		if (objects.empty())
			SPRaise("Cannot save empty .2kv6 scene");

		if (objects.size() > UINT16_MAX)
			SPRaise("Too many root objects in .2kv6 scene: %zu (max %u)", objects.size(),
			        UINT16_MAX);

		// Write file header (8 bytes total)
		KV6FileHeader header;
		std::strncpy(header.magic, MAGIC, 4);
		header.version = FORMAT_VERSION;
		header.numRootObjects = static_cast<uint16_t>(objects.size());
		stream.Write(&header, sizeof(header));

		// Recursively write objects
		for (const VoxelObject& obj : objects) {
			SaveObject(stream, obj, 0);
		}

		stream.Flush();
	}

	VoxelObject VoxelModel2KV6::CreateObject(const std::string& name, int sizeXYZ) {
		VoxelObject obj;
		obj.name = name;
		obj.model = Handle<VoxelModel>::New(sizeXYZ, sizeXYZ, sizeXYZ);
		obj.model->SetSolid(sizeXYZ / 2, sizeXYZ / 2, sizeXYZ / 2, 0xFFFFFF);
		float c = float(sizeXYZ / 2);
		obj.model->SetOrigin(MakeVector3(-c, -c, -c));
		return obj;
	}

	bool VoxelModel2KV6::DeleteObject(VoxelObject& parent, size_t childIndex) {
		if (childIndex >= parent.children.size())
			return false;
		parent.children.erase(parent.children.begin() + childIndex);
		return true;
	}

	VoxelObject* VoxelModel2KV6::FindObjectByName(std::vector<VoxelObject>& scene,
	                                               const std::string& name) {
		// Breadth-first search for named object
		std::vector<VoxelObject*> queue;
		for (auto& obj : scene)
			queue.push_back(&obj);

		while (!queue.empty()) {
			VoxelObject* current = queue.front();
			queue.erase(queue.begin());

			if (current->name == name)
				return current;

			for (auto& child : current->children)
				queue.push_back(&child);
		}

		return nullptr;
	}

} // namespace spades
