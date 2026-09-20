/*
 Copyright (c) 2019 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "VoxelModelLoader.h"

#include <json/json.h>

#include <algorithm>
#include <functional>
#include <tuple>

#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/LocalFileSystem.h>
#include <Core/Math.h>
#include <Core/TMPUtils.h>
#include <Core/VoxelModel.h>
#include <Core/VoxelModel2KV6.h>

namespace spades {
	namespace {
		// Merge all objects in a scene hierarchy into a single composite VoxelModel
		// with transforms applied (positions, rotations, scales).
		Handle<VoxelModel> MergeSceneObjects(const std::vector<VoxelObject>& scene) {
			if (scene.empty())
				SPRaise(".2kv6 scene is empty");

			// Find bounds of all objects
			int minX = 0, minY = 0, minZ = 0;
			int maxX = 0, maxY = 0, maxZ = 0;
			bool hasAny = false;

			// First pass: collect all voxel positions with transforms
			std::vector<std::tuple<int, int, int, uint32_t>> voxels; // x, y, z, color

			std::function<void(const VoxelObject&, const Matrix4&)> collectVoxels =
			    [&](const VoxelObject& obj, const Matrix4& parentTransform) {
				    // Build local transform: translate * rotate * scale
				    Matrix4 scale = Matrix4::Scale(obj.scale);
				    Matrix4 rotate = Quaternion(obj.rotation).ToRotationMatrix();
				    Matrix4 translate = Matrix4::Translate(obj.position);
				    Matrix4 localTransform = translate * rotate * scale;

				    // Combine with parent transform
				    Matrix4 worldTransform = parentTransform * localTransform;

				    // Collect voxels from this object's model
				    if (obj.model) {
					    for (int x = 0; x < obj.model->GetWidth(); x++) {
						    for (int y = 0; y < obj.model->GetHeight(); y++) {
							    for (int z = 0; z < obj.model->GetDepth(); z++) {
								    if (!obj.model->IsSolid(x, y, z))
									    continue;

								    // Transform voxel position
								    Vector3 pos = MakeVector3(float(x), float(y), float(z));
								    Vector4 transformed4 = worldTransform * MakeVector4(pos.x, pos.y, pos.z, 1.0F);
								    Vector3 transformed = MakeVector3(transformed4.x, transformed4.y, transformed4.z);

								    int tx = int(std::round(transformed.x));
								    int ty = int(std::round(transformed.y));
								    int tz = int(std::round(transformed.z));

								    uint32_t color = obj.model->GetColor(x, y, z);
								    voxels.push_back(std::make_tuple(tx, ty, tz, color));

								    if (!hasAny) {
									    minX = maxX = tx;
									    minY = maxY = ty;
									    minZ = maxZ = tz;
									    hasAny = true;
								    } else {
									    minX = std::min(minX, tx);
									    maxX = std::max(maxX, tx);
									    minY = std::min(minY, ty);
									    maxY = std::max(maxY, ty);
									    minZ = std::min(minZ, tz);
									    maxZ = std::max(maxZ, tz);
								    }
							    }
						    }
					    }
				    }

				    // Recursively process children
				    for (const VoxelObject& child : obj.children) {
					    collectVoxels(child, worldTransform);
				    }
			    };

			// Collect voxels from all root objects
			Matrix4 identity;
			for (const VoxelObject& root : scene) {
				collectVoxels(root, identity);
			}

			if (!hasAny)
				SPRaise(".2kv6 scene has no voxel data");

			// Create composite model with proper dimensions
			int width = maxX - minX + 1;
			int height = maxY - minY + 1;
			int depth = maxZ - minZ + 1;

			Handle<VoxelModel> result = Handle<VoxelModel>::New(width, height, depth);

			// Place all voxels in the composite model
			for (const auto& voxel : voxels) {
				int x = std::get<0>(voxel);
				int y = std::get<1>(voxel);
				int z = std::get<2>(voxel);
				uint32_t color = std::get<3>(voxel);

				int dx = x - minX;
				int dy = y - minY;
				int dz = z - minZ;

				if (dx >= 0 && dx < width && dy >= 0 && dy < height && dz >= 0 && dz < depth) {
					result->SetSolid(dx, dy, dz, color);
				}
			}

			return result;
		}

		// Copied from `ngspades`, a cancelled branch of OpenSpades
		Vector3 ReadVector3(const Json::Value& json, const char* name) {
			if (json.isArray() && json.size() == 3) {
				auto e1 = json.get((Json::UInt)0, Json::nullValue);
				auto e2 = json.get((Json::UInt)1, Json::nullValue);
				auto e3 = json.get((Json::UInt)2, Json::nullValue);
				if (e1.isConvertibleTo(Json::ValueType::realValue) &&
				    e2.isConvertibleTo(Json::ValueType::realValue) &&
				    e3.isConvertibleTo(Json::ValueType::realValue))
					return Vector3((float)e1.asDouble(), (float)e2.asDouble(),
					               (float)e3.asDouble());
			}

			SPRaise("%s must be vector consisting of three real values", name);
		}

		struct Metadata {
			stmp::optional<Vector3> origin;
			stmp::optional<MaterialType> forceMaterial;

			void Parse(const Json::Value& root) {
				if (root.type() == Json::objectValue) {
					auto jsonOrigin = root.get("Origin", Json::nullValue);
					if (!jsonOrigin.isNull())
						origin = ReadVector3(jsonOrigin, "Origin");

					auto jsonForceMaterial = root.get("ForceMaterial", Json::nullValue);
					if (!jsonForceMaterial.isNull()) {
						if (!jsonForceMaterial.isString())
							SPRaise("ForceMaterial must be a string");

						auto str = jsonForceMaterial.asString();
						if (str == "Default")
							forceMaterial = MaterialType::Default;
						else if (str == "Emissive")
							forceMaterial = MaterialType::Emissive;
						else
							SPRaise("ForceMaterial: Unrecognized value '%s'", str.c_str());
					}
				}
			}
		};
	} // namespace

	Handle<VoxelModel> VoxelModelLoader::Load(const char* path) {
		std::string pathStr(path);

		// Detect format from extension
		bool is2KV6 = LocalFileSystem::HasExtension(pathStr, ".2kv6");

		// Load the metadata file
		std::string metadataPath = pathStr;
		{
			auto i = metadataPath.rfind('.');
			if (i != std::string::npos)
				metadataPath.resize(i);

			metadataPath += ".meta.json";
		}

		// Load the metadata
		Metadata meta;
		if (FileManager::FileExists(metadataPath.c_str())) {
			SPLog("Found a metadata file '%s', loading it...", metadataPath.c_str());
			std::string metadataJson = FileManager::ReadAllBytes(metadataPath.c_str());

			Json::Reader reader;
			Json::Value root;

			if (reader.parse(metadataJson, root, false)) {
				meta.Parse(root);
			} else {
				SPRaise("The voxel model metadata file is not a valid JSON file.");
			}
		}

		// Load the model (either .kv6 or .2kv6)
		Handle<VoxelModel> voxelModel;
		if (is2KV6) {
			// Load .2kv6 scene and merge all objects into a single composite model
			SPLog("Loading '%s' as a .2kv6 scene file.", path);
			std::unique_ptr<IStream> stream{FileManager::OpenForReading(path)};
			std::vector<VoxelObject> scene = VoxelModel2KV6::Load(*stream);
			voxelModel = MergeSceneObjects(scene);
		} else {
			// Load .kv6 file (regular single-model)
			SPLog("Loading '%s' as a KV6 voxel model.", path);
			std::unique_ptr<IStream> stream{FileManager::OpenForReading(path)};
			voxelModel = VoxelModel::LoadKV6(*stream);
		}

		// Apply transformation requested by the metadata
		if (meta.origin)
			voxelModel->SetOrigin(*meta.origin);
		if (meta.forceMaterial)
			voxelModel->ForceMaterial(*meta.forceMaterial);

		return voxelModel;
	}
} // namespace spades