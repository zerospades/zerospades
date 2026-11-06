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

#include "PlayerModels.h"

#include <exception>
#include <memory>

#include <Core/Debug.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/VoxelModel.h>

#include "IModel.h"
#include "IRenderer.h"

namespace spades {
	namespace client {
		namespace {
			const char* const kSceneFile = "Player.2kv6";
			const char* const kHatPath = "Body/Neck/XmasHat";

			// Where the template keeps each classic part.
			const char* ScenePathOf(const std::string& name) {
				static const std::map<std::string, const char*> paths = {
				  {"Torso", "Body/Torso"},
				  {"Head", "Body/Neck/Head"},
				  {"Arms", "Body/Shoulders/Arms"},
				  {"Leg", "HipLeft/Leg"},
				  {"LegRight", "HipRight/LegRight"},
				  {"TorsoCrouch", "Crouch/Body/TorsoCrouch"},
				  {"LegCrouch", "Crouch/HipLeft/LegCrouch"},
				  {"LegRightCrouch", "Crouch/HipRight/LegRightCrouch"},
				  {"UpperArm", "FirstPerson/UpperLeft/UpperArm"},
				  {"Arm", "FirstPerson/ForeLeft/Arm"},
				  {"UpperArmRight", "FirstPerson/UpperRight/UpperArmRight"},
				  {"ArmRight", "FirstPerson/ForeRight/ArmRight"},
				  {"Dead", "Dead/Dead"},
				};
				const auto it = paths.find(name);
				return it == paths.end() ? nullptr : it->second;
			}

			// The object `path` names ("Body/Neck/Head"); null when there is
			// none, or several share a name on the way.
			VoxelObject* Find(std::vector<VoxelObject>& scene, const std::string& path) {
				std::vector<VoxelObject>* level = &scene;
				VoxelObject* found = nullptr;
				std::size_t start = 0;
				while (true) {
					const std::size_t slash = path.find('/', start);
					const std::string name = path.substr(
					  start, slash == std::string::npos ? std::string::npos : slash - start);
					found = nullptr;
					for (VoxelObject& o : *level) {
						if (o.name != name)
							continue;
						if (found)
							return nullptr;
						found = &o;
					}
					if (!found || slash == std::string::npos)
						return found;
					level = &found->children;
					start = slash + 1;
				}
			}
		} // namespace

		PlayerModels::PlayerModels(IRenderer& renderer) : renderer(renderer) {}
		PlayerModels::~PlayerModels() {}

		const std::vector<std::string>& PlayerModels::PartNames() {
			static const std::vector<std::string> names = {
			  "Arm",   "ArmRight",       "Arms",  "Dead",        "Head",     "Leg",          "LegCrouch",
			  "LegRight", "LegRightCrouch", "Torso", "TorsoCrouch", "UpperArm", "UpperArmRight"};
			return names;
		}

		PlayerModels::Set& PlayerModels::SetIn(const std::string& dir) {
			Set& set = sets[dir];
			if (set.loaded)
				return set;
			set.loaded = true;
			const std::string path = dir + kSceneFile;
			set.sceneRank = FileManager::Rank(path.c_str());
			if (set.sceneRank < 0)
				return set; // a classic set
			try {
				std::unique_ptr<IStream> stream{FileManager::OpenForReading(path.c_str())};
				set.scene = VoxelModel2KV6::Load(*stream);
				SPLog("Player models in '%s' come from its scene", dir.c_str());
			} catch (const std::exception& ex) {
				// A scene that cannot be read leaves the set its .kv6 files.
				SPLog("Could not read '%s', using its .kv6 parts: %s", path.c_str(), ex.what());
				set.scene.clear();
				set.sceneRank = -1;
			}
			return set;
		}

		PlayerPart PlayerModels::ScenePart(Set& set, const std::string& path) {
			const auto cached = set.parts.find(path);
			if (cached != set.parts.end())
				return cached->second;
			PlayerPart part;
			if (VoxelObject* o = Find(set.scene, path)) {
				if (o->model) {
					// Voxel `i` lands at its place on the hinge times `i`, as a
					// classic part's lands at its pivot plus `i`: the model's own
					// origin plays no part in a scene.
					o->model->SetOrigin(MakeVector3(0.0F, 0.0F, 0.0F));
					part.model = renderer.CreateModel(*o->model);
					part.local = LocalTransform(o->position, o->rotation, o->scale);
				}
			}
			set.parts[path] = part;
			return part;
		}

		PlayerPart PlayerModels::Get(const std::string& dir, const std::string& name) {
			Set& set = SetIn(dir);
			const std::string file = dir + name + ".kv6";
			const char* path = ScenePathOf(name);
			if (!set.scene.empty() && path) {
				// The scene's, unless a stronger pack gives this part's .kv6.
				const int fileRank = FileManager::Rank(file.c_str());
				if (fileRank < 0 || set.sceneRank <= fileRank) {
					PlayerPart part = ScenePart(set, path);
					if (part)
						return part;
				}
			}
			PlayerPart part;
			part.model = renderer.RegisterModel(file.c_str());
			return part;
		}

		PlayerPart PlayerModels::Hat(const std::string& dir) {
			Set& set = SetIn(dir);
			if (set.scene.empty())
				return PlayerPart();
			// Fitted to the scene's head: a head a stronger pack gives as a
			// .kv6 is a classic one, which takes the classic hatted head.
			const int headRank = FileManager::Rank((dir + "Head.kv6").c_str());
			if (headRank >= 0 && headRank < set.sceneRank)
				return PlayerPart();
			return ScenePart(set, kHatPath);
		}

		void PlayerModels::Preload(const std::string& dir, bool christmas) {
			for (const std::string& name : PartNames())
				Get(dir, name);
			if (christmas)
				Hat(dir);
		}
	} // namespace client
} // namespace spades
