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
#include <ScriptBindings/ScriptManager.h>

#include "IModel.h"
#include "IRenderer.h"

namespace spades {
	namespace client {
		namespace {
			const char* const kSetsDir = "Models/Player/";
			// Where a scene's head wears the Christmas hat every set shares: a
			// hinge the hat's pivot sits on.
			const char* const kHatPath = "Player/Neck/SantaHat";
			const char* const kHatFile = "Models/Player/SantaHat.kv6";

			// Where the template keeps each classic part (player-scene.sh).
			const char* ScenePathOf(const std::string& name) {
				static const std::map<std::string, const char*> paths = {
				  {"Torso", "Player/Standing/UpperBody/Torso"},
				  {"Head", "Player/Neck/Head"},
				  {"Arms", "Player/Shoulders/ArmsHolding"},
				  {"Leg", "Player/Standing/LeftHip/LeftLeg"},
				  {"LegRight", "Player/Standing/RightHip/RightLeg"},
				  {"TorsoCrouch", "Player/Crouching/UpperBody/Torso"},
				  {"LegCrouch", "Player/Crouching/LeftHip/LeftLeg"},
				  {"LegRightCrouch", "Player/Crouching/RightHip/RightLeg"},
				  {"UpperArm", "Player/JointedArms/LeftShoulder/LeftUpperArm"},
				  {"Arm", "Player/JointedArms/LeftShoulder/LeftUpperArm/LeftElbow/LeftForearm"},
				  {"UpperArmRight", "Player/JointedArms/RightShoulder/RightUpperArm"},
				  {"ArmRight", "Player/JointedArms/RightShoulder/RightUpperArm/RightElbow/RightForearm"},
				  {"Dead", "Fallen/Corpse"},
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

		std::string PlayerModels::SetName(const std::string& dir) {
			// "Models/Player/" is the classic set, "Models/Player/Rifle/" Rifle.
			std::string name = dir.compare(0, std::string(kSetsDir).size(), kSetsDir) == 0
			                     ? dir.substr(std::string(kSetsDir).size())
			                     : dir;
			if (!name.empty() && name.back() == '/')
				name.pop_back();
			return name.empty() ? std::string("Classic") : name;
		}

		PlayerModels::Set& PlayerModels::SetIn(const std::string& dir) {
			Set& set = sets[dir];
			if (set.loaded)
				return set;
			set.loaded = true;
			// The set's script says what it is made of: the scene to load, or
			// nothing for its classic .kv6 parts. One that is missing (a pack
			// replacing the skin scripts from before there were any) or fails
			// says nothing: the classic parts, as they always were.
			const std::string script = "Scripts/Skin/Player/" + SetName(dir) + ".as";
			std::string declared;
			try {
				declared = Declared(script);
			} catch (const std::exception& ex) {
				SPLog("Player models in '%s': %s says nothing (%s); using its .kv6 parts",
				      dir.c_str(), script.c_str(), ex.what());
				return set;
			}
			if (declared.empty()) {
				SPLog("Player models in '%s': its .kv6 parts, as %s says", dir.c_str(),
				      script.c_str());
				return set;
			}
			// In the set's own folder, unless it names another.
			if (declared.compare(0, 7, "Models/") != 0)
				declared = dir + declared;
			set.declaredRank = FileManager::Rank(script.c_str());
			try {
				std::unique_ptr<IStream> stream{FileManager::OpenForReading(declared.c_str())};
				set.scene = VoxelModel2KV6::Load(*stream);
				SPLog("Player models in '%s': %s, as %s says", dir.c_str(), declared.c_str(),
				      script.c_str());
			} catch (const std::exception& ex) {
				// A scene that cannot be read leaves the set its .kv6 files.
				SPLog("Could not read '%s', using the .kv6 parts in '%s': %s", declared.c_str(),
				      dir.c_str(), ex.what());
				set.scene.clear();
			}
			return set;
		}

		std::string PlayerModels::Declared(const std::string& script) {
			// Every set's script is the same function, so each is built as a
			// module of its own, apart from the client's and the others.
			const std::string source = FileManager::ReadAllBytes(script.c_str());
			ScriptManager& manager = *ScriptManager::GetInstance();
			asIScriptEngine* engine = manager.GetEngine();
			const std::string moduleName = "PlayerModels " + script;
			asIScriptModule* module = engine->GetModule(moduleName.c_str(), asGM_ALWAYS_CREATE);
			engine->SetDefaultNamespace("");
			ScriptManager::CheckError(
			  module->AddScriptSection(script.c_str(), source.c_str(), source.size()));
			if (module->Build() < 0)
				SPRaise("%s does not build", script.c_str());
			asIScriptFunction* function = nullptr;
			for (const char* ns : {"spades", ""}) {
				module->SetDefaultNamespace(ns);
				function = module->GetFunctionByDecl("string PlayerModels()");
				if (function)
					break;
			}
			if (!function)
				SPRaise("%s has no 'string PlayerModels()'", script.c_str());
			ScriptContextHandle ctx = manager.GetContext();
			ScriptManager::CheckError(ctx->Prepare(function));
			ctx.ExecuteChecked();
			const std::string declared = *reinterpret_cast<std::string*>(ctx->GetReturnObject());
			engine->DiscardModule(moduleName.c_str());
			return declared;
		}

		bool PlayerModels::ClassicWins(const Set& set, const std::string& file) const {
			// A part shipped from a pack above the script that chose the scene
			// (a skin made before there were scenes) is that skin's own.
			const int rank = FileManager::Rank(file.c_str());
			return rank >= 0 && rank < set.declaredRank;
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
			if (!set.scene.empty() && path && !ClassicWins(set, file)) {
				PlayerPart part = ScenePart(set, path);
				if (part)
					return part; // else the scene lacks it: the .kv6
			}
			PlayerPart part;
			part.model = renderer.RegisterModel(file.c_str());
			return part;
		}

		PlayerPart PlayerModels::Hat(const std::string& dir) {
			Set& set = SetIn(dir);
			if (set.scene.empty())
				return PlayerPart();
			// Fitted to the scene's head: a classic head that wins over it takes
			// the classic hatted head instead.
			if (ClassicWins(set, dir + "Head.kv6"))
				return PlayerPart();
			const VoxelObject* mount = Find(set.scene, kHatPath);
			if (!mount)
				return PlayerPart();
			PlayerPart hat;
			hat.model = renderer.RegisterModel(kHatFile);
			hat.local = LocalTransform(mount->position, mount->rotation, mount->scale);
			return hat;
		}

		void PlayerModels::Preload(const std::string& dir, bool christmas) {
			const Set& set = SetIn(dir);
			for (const std::string& name : PartNames()) {
				// Said once, as it is decided: a skin's maker sees which of
				// their parts the game took.
				if (!set.scene.empty() && ClassicWins(set, dir + name + ".kv6"))
					SPLog("Player models in '%s': %s.kv6, from a pack above the set's script, "
					      "takes the scene's place",
					      dir.c_str(), name.c_str());
				Get(dir, name);
			}
			if (christmas)
				Hat(dir);
		}
	} // namespace client
} // namespace spades
