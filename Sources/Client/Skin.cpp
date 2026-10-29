/*
 Copyright (c) 2026 ZeroSpades developers.

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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include <cstdint>

#include "Skin.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/GameAsset.h>
#include <Core/VoxelModelLoader.h>
#include <ScriptBindings/ScriptManager.h>

namespace spades {
	namespace client {
		Handle<Skin> Skin::Load(const std::string& name) {
			SPADES_MARK_FUNCTION();

			std::vector<std::string> paths = ModPak::FindPaths(name);
			if (paths.empty())
				SPRaise("There is no mod named '%s' in Mods/.", name.c_str());

			Handle<Skin> skin{new Skin(), false};
			skin->name = name;
			for (const std::string& path : paths)
				skin->paks.push_back(ModPak::Load(path, name));

			bool hasScripts = false;
			for (const auto& pak : skin->paks) {
				for (const std::string& file : pak->GetFiles()) {
					if (file.empty() || file.back() == '/')
						continue;
					switch (GetGameAssetKind(file)) {
						case GameAssetKind::None:
							SPLog("Skin '%s': '%s' is not a game asset, left out", name.c_str(),
							      file.c_str());
							continue;
						case GameAssetKind::Script: hasScripts = true; break;
						// It changes the model beside it.
						case GameAssetKind::ModelMetadata:
							skin->assets.push_back(GetModelPathOfMetadata(file));
							break;
						default: skin->assets.push_back(GetAssetRequestPath(file)); break;
					}
					skin->files.insert(NormalizeResourcePath(file));
				}
			}

			if (hasScripts) {
				// A skin loaded again under the same name is another skin, with its
				// own module: the old one's may still be around, and goes with it.
				static std::uint64_t serial = 0;
				std::string module = "Skin:" + name + "#" + std::to_string(++serial);
				std::string file;
				std::string error;
				{
					Scope scope(*skin);
					error = ScriptManager::GetInstance()->BuildModule(module, file);
				}
				if (!error.empty())
					SPRaise("%s", error.c_str());
				skin->scriptModule = module;
			}
			return skin;
		}

		Skin::~Skin() {
			if (scriptModule != "Client")
				ScriptManager::GetInstance()->DiscardModule(scriptModule);
		}

		bool Skin::Has(const std::string& path) const {
			return files.count(NormalizeResourcePath(path)) != 0 ||
			       files.count(NormalizeResourcePath(VoxelModelLoader::GetMetadataPath(path))) != 0;
		}

		std::vector<IFileSystem*> Skin::GetFileSystems() const {
			std::vector<IFileSystem*> fileSystems;
			// The last pak is on top.
			for (auto it = paks.rbegin(); it != paks.rend(); ++it)
				fileSystems.push_back((*it)->GetFileSystem());
			return fileSystems;
		}

		Skin::Scope::Scope(const Skin& skin) : overlay(skin.GetFileSystems()) {}
	} // namespace client
} // namespace spades
