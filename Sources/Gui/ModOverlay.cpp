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

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Main.h"
#include <ZeroSpades.h>
#include "ModOverlay.h"
#include "ModsScreenHelper.h"
#include <Client/GameResources.h>
#include <Core/Debug.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/GameAsset.h>
#include <Core/IFileSystem.h>
#include <Core/ModPak.h>
#include <Core/VoxelModelLoader.h>
#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Core/Strings.h>
#include <ScriptBindings/ScriptManager.h>

namespace spades {
	namespace gui {
		namespace {
			std::vector<std::unique_ptr<ModPak>> g_paks; // mounted, the last on top
			std::vector<std::string> g_mods;             // the enabled set, as mounted
			std::vector<ModFailure> g_failures;

			// The paks whose game assets were checked and loaded, by id and the
			// build that checked them: another build may load files differently.
			constexpr const char* kCheckedPaksFile = "ModsChecked.txt";
			std::set<std::string> g_checked; // keys, as ReadCheckedPaks returns

			std::string GetCheckKey(const ModPak& pak) {
				return pak.GetId() + " " + ZEROSPADES_VER_STR;
			}

			std::set<std::string> ReadCheckedPaks() {
				std::set<std::string> keys;
				try {
					if (!FileManager::FileExists(kCheckedPaksFile))
						return keys;
					std::string all = FileManager::ReadAllBytes(kCheckedPaksFile);
					std::size_t start = 0;
					while (start < all.size()) {
						std::size_t end = all.find('\n', start);
						if (end == std::string::npos)
							end = all.size();
						if (end > start)
							keys.insert(all.substr(start, end - start));
						start = end + 1;
					}
				} catch (const std::exception& ex) {
					SPLog("%s not read: %s", kCheckedPaksFile, ex.what());
				}
				return keys;
			}

			void WriteCheckedPaks() {
				try {
					std::string all;
					for (const std::string& key : g_checked)
						all += key + "\n";
					auto file = FileManager::OpenForWriting(kCheckedPaksFile);
					file->Write(all);
				} catch (const std::exception& ex) {
					SPLog("%s not written: %s", kCheckedPaksFile, ex.what());
				}
			}

			using PakSet = std::vector<std::unique_ptr<ModPak>>;

			// Mounts `paks` in place of `mounted`, in order: the last on top.
			void Swap(const PakSet& mounted, const PakSet& paks) {
				for (const auto& pak : mounted)
					pak->Unmount();
				for (const auto& pak : paks)
					pak->Mount();
			}

			// The pak each file of `paks` is read from, by normalized path. As in
			// FileManager, a plain file beats any `.weak` one, then the topmost wins.
			struct Winner {
				std::string file; // as the pak spells it
				const ModPak* pak;
				bool weak;
			};
			std::map<std::string, Winner> GetWinners(const PakSet& paks) {
				std::map<std::string, Winner> winners;
				for (const auto& pak : paks) {
					for (const std::string& file : pak->GetFiles()) {
						if (file.empty() || file.back() == '/')
							continue;
						std::string key = NormalizeResourcePath(file);
						bool weak = key.size() != file.size();
						auto it = winners.find(key);
						if (it != winners.end() && weak && !it->second.weak)
							continue;
						winners[key] = {file, pak.get(), weak};
					}
				}
				return winners;
			}

			// The files read from another pak, or from none, once `after` is
			// mounted in place of `before`.
			std::vector<std::string> GetChanges(const PakSet& before, const PakSet& after) {
				auto old = GetWinners(before);
				auto now = GetWinners(after);
				std::vector<std::string> changes;
				for (const auto& file : old) {
					auto it = now.find(file.first);
					if (it == now.end() || it->second.pak->GetId() != file.second.pak->GetId())
						changes.push_back(file.second.file);
				}
				for (const auto& file : now) {
					if (old.find(file.first) == old.end())
						changes.push_back(file.second.file);
				}
				return changes;
			}

			// The mod of `paks` the game reads `path` from, or "".
			std::string GetOwner(const PakSet& paks, const std::string& path) {
				auto winners = GetWinners(paks);
				auto it = winners.find(NormalizeResourcePath(path));
				return it == winners.end() ? std::string() : it->second.pak->GetMod();
			}

			// The first of `paks`' mods with a game asset that doesn't load, and
			// why. A pak that passed before, unchanged since and checked by this
			// build, is not checked again; one that passes now is remembered, unless
			// what it loaded depends on another pak: a model's metadata from elsewhere.
			std::map<std::string, std::string> CheckAssets(const PakSet& paks) {
				std::set<std::string> passed = ReadCheckedPaks();
				std::map<std::string, std::string> broken; // mod -> why
				for (const auto& pak : paks) {
					std::string key = GetCheckKey(*pak);
					if (passed.count(key)) {
						g_checked.insert(key);
						continue;
					}
					if (broken.count(pak->GetMod()))
						continue;

					std::set<std::string> own;
					for (const std::string& file : pak->GetFiles())
						own.insert(NormalizeResourcePath(file));

					// Its own files, not what another mod puts over them.
					FileManager::Overlay overlay({pak->GetFileSystem()});
					bool selfContained = true;
					for (const std::string& file : pak->GetFiles()) {
						GameAssetKind kind = GetGameAssetKind(file);
						if (kind == GameAssetKind::None || kind == GameAssetKind::Script)
							continue;
						// By the name the game asks for it by.
						std::string path = GetAssetRequestPath(file);
						if (kind == GameAssetKind::Model) {
							std::string meta = VoxelModelLoader::GetMetadataPath(path);
							if (!own.count(NormalizeResourcePath(meta)) &&
							    FileManager::FileExists(meta.c_str()))
								selfContained = false;
						}
						try {
							client::GameResources::CheckFile(path);
						} catch (const std::exception& ex) {
							broken.emplace(pak->GetMod(), file + ": " + ex.what());
							break;
						}
					}
					if (!broken.count(pak->GetMod()) && selfContained)
						g_checked.insert(key);
				}
				return broken;
			}

			void RemoveMod(PakSet& paks, std::vector<std::string>& mods, const std::string& mod) {
				paks.erase(std::remove_if(paks.begin(), paks.end(),
				                          [&](const std::unique_ptr<ModPak>& pak) {
					                          return pak->GetMod() == mod;
				                          }),
				           paks.end());
				mods.erase(std::remove(mods.begin(), mods.end(), mod), mods.end());
			}

			bool HasScripts(const ModPak& pak) {
				for (const std::string& file : pak.GetFiles()) {
					if (GetGameAssetKind(file) == GameAssetKind::Script)
						return true;
				}
				return false;
			}

			using Fail = std::function<void(const std::string& mod, const std::string& why)>;

			/**
			 * Leaves mods with broken scripts out of `paks`, which are mounted, until
			 * the scripts build: "Client" is then rebuilt. `error` and `file` are the
			 * failed build's. The first suspect whose absence makes the scripts build
			 * is at fault; when none does alone, more than one is broken, and the
			 * likeliest goes. `fail` removes a mod and its paks from the set. Returns
			 * "", or the error no mod can be blamed for.
			 *
			 * The suspects: the mod the failing script comes from, then the mods
			 * with scripts not in `before` (the ones a change brings), then the
			 * others with scripts, the last first. AngelScript reports an error where
			 * it notices it, which can be in another mod's file than the one at fault.
			 */
			std::string FixScripts(PakSet& paks, std::string error, std::string file,
			                       const std::set<std::string>& before, const Fail& fail) {
				ScriptManager& scripts = *ScriptManager::GetInstance();
				while (!error.empty()) {
					std::vector<std::string> suspects;
					auto suspect = [&](const std::string& mod) {
						if (!mod.empty() &&
						    std::find(suspects.begin(), suspects.end(), mod) == suspects.end())
							suspects.push_back(mod);
					};
					if (!file.empty())
						suspect(GetOwner(paks, file));
					for (auto it = paks.rbegin(); it != paks.rend(); ++it) {
						if (!before.count((*it)->GetId()) && HasScripts(**it))
							suspect((*it)->GetMod());
					}
					for (auto it = paks.rbegin(); it != paks.rend(); ++it) {
						if (HasScripts(**it))
							suspect((*it)->GetMod());
					}
					if (suspects.empty())
						return error;

					for (const std::string& mod : suspects) {
						for (const auto& pak : paks) {
							if (pak->GetMod() == mod)
								pak->Unmount();
						}
						std::string ignored;
						if (scripts.RebuildClientModule(ignored).empty()) {
							fail(mod, error);
							return {};
						}
						Swap(paks, paks);
					}

					const std::string mod = suspects.front();
					for (const auto& pak : paks) {
						if (pak->GetMod() == mod)
							pak->Unmount();
					}
					fail(mod, error);
					error = scripts.RebuildClientModule(file);
				}
				return {};
			}

			// Why `mod` changing `file` takes a restart, for the user. `menus`: the
			// menus loaded the file.
			std::string DescribeRestart(const std::string& mod, const std::string& file,
			                            bool menus) {
				std::string p = NormalizeResourcePath(file);
				auto startsWith = [&](const char* prefix) { return p.rfind(prefix, 0) == 0; };
				auto endsWith = [&](const char* suffix) {
					std::size_t n = std::char_traits<char>::length(suffix);
					return p.size() >= n && p.compare(p.size() - n, n, suffix) == 0;
				};
				if (menus)
					return _Tr("ModOverlay", "{0} changes '{1}', which the menus use.", mod, file);
				if (startsWith("gfx/fonts/") || startsWith("fonts/") || endsWith(".ttf") ||
				    endsWith(".otf") || endsWith(".ttc"))
					return _Tr("ModOverlay", "{0} changes the game's fonts ({1}).", mod, file);
				if (startsWith("shaders/"))
					return _Tr("ModOverlay", "{0} changes the game's shaders ({1}).", mod, file);
				if (startsWith("locales/"))
					return _Tr("ModOverlay", "{0} changes the game's translations ({1}).", mod,
					           file);
				return _Tr("ModOverlay", "{0} changes '{1}', which the game reads when it starts.",
				           mod, file);
			}

			// Takes the mod down and out of the enabled set, on disk too.
			void Disable(const std::string& mod, const std::string& reason) {
				SPLog("Mod failed to load, disabled: %s: %s", mod.c_str(), reason.c_str());
				for (const auto& pak : g_paks) {
					if (pak->GetMod() == mod)
						pak->Unmount();
				}
				RemoveMod(g_paks, g_mods, mod);
				Handle<ModsScreenHelper>::New()->SetEnabledMods(g_mods);
				g_failures.push_back({mod, reason});
			}
		} // namespace

		void ModOverlay::Mount() {
			SPADES_MARK_FUNCTION();

			// A --try-mod run applies the mod under test on top of the base paks only.
			if (g_tryMod)
				return;

			g_mods = Handle<ModsScreenHelper>::New()->GetEnabledMods();

			std::map<std::string, std::string> broken; // mod -> why
			for (const auto& pak : ModsScreenHelper::GetModPaks(g_mods)) {
				if (broken.count(pak.first))
					continue;
				try {
					g_paks.push_back(ModPak::Load(pak.second, pak.first));
					g_paks.back()->Mount();
					SPLog("Mod pak mounted (in memory): %s", pak.second.c_str());
				} catch (const std::exception& ex) {
					broken[pak.first] = pak.second + ": " + ex.what();
				}
			}
			for (const auto& mod : broken)
				Disable(mod.first, mod.second);

			// Check every game asset the mods bring loads, so a broken one disables
			// its mod now rather than failing in a game. A pak that passed before,
			// unchanged since, is not checked again.
			for (const auto& mod : CheckAssets(g_paks))
				Disable(mod.first, mod.second);
			WriteCheckedPaks();
		}

		void ModOverlay::StartScripts() {
			SPADES_MARK_FUNCTION();

			std::string file;
			std::string error = ScriptManager::GetInstance()->GetStartupError(file);
			if (error.empty())
				return;
			error = FixScripts(g_paks, error, file, {}, Disable);
			if (!error.empty())
				throw ScriptBuildError(error, file);
		}

		ModApplyResult ModOverlay::Apply(client::IRenderer& renderer, client::IAudioDevice& audio,
		                                 std::vector<ModFailure>& failures, std::string& error) {
			SPADES_MARK_FUNCTION();

			if (g_tryMod) {
				error = _Tr("ModOverlay", "Mods can't be applied while trying a mod.");
				return ModApplyResult::Refused;
			}

			Handle<ModsScreenHelper> helper = Handle<ModsScreenHelper>::New();
			std::vector<std::string> mods = helper->GetEnabledMods();
			auto fail = [&](PakSet& paks, const std::string& mod, const std::string& reason) {
				SPLog("Mod failed to load, not applied: %s: %s", mod.c_str(), reason.c_str());
				RemoveMod(paks, mods, mod);
				failures.push_back({mod, reason});
			};

			// Read the new set and check its game assets, before mounting anything.
			PakSet paks;
			std::map<std::string, std::string> broken;
			for (const auto& pak : ModsScreenHelper::GetModPaks(mods)) {
				if (broken.count(pak.first))
					continue;
				try {
					paks.push_back(ModPak::Load(pak.second, pak.first));
				} catch (const std::exception& ex) {
					broken[pak.first] = pak.second + ": " + ex.what();
				}
			}
			for (const auto& mod : broken)
				fail(paks, mod.first, mod.second);
			for (const auto& mod : CheckAssets(paks))
				fail(paks, mod.first, mod.second);

			std::vector<std::string> changes = GetChanges(g_paks, paks);

			// The software's own files were loaded from the mounted set: a change
			// to them, or to a file that isn't a game asset, waits for a restart.
			std::set<std::string> loaded;
			for (const std::string& file : renderer.GetRegisteredFiles()) {
				loaded.insert(NormalizeResourcePath(file));
				loaded.insert(NormalizeResourcePath(VoxelModelLoader::GetMetadataPath(file)));
			}
			for (const std::string& file : audio.GetRegisteredFiles())
				loaded.insert(NormalizeResourcePath(file));
			bool scripts = false;
			for (const std::string& file : changes) {
				if (IsUnreadModFile(file))
					continue;
				GameAssetKind kind = GetGameAssetKind(file);
				bool menus = loaded.count(NormalizeResourcePath(file)) != 0;
				if (kind == GameAssetKind::None || menus) {
					// The mod it comes from, or went with.
					std::string mod = GetOwner(paks, file);
					if (mod.empty())
						mod = GetOwner(g_paks, file);
					error = DescribeRestart(mod, file, menus);
					SPLog("Mods need a restart: '%s' changed", file.c_str());
					helper->SetEnabledMods(mods);
					return ModApplyResult::NeedsRestart;
				}
				if (kind == GameAssetKind::Script)
					scripts = true;
			}

			Swap(g_paks, paks);

			if (scripts) {
				std::string file;
				std::string scriptError = ScriptManager::GetInstance()->RebuildClientModule(file);
				std::set<std::string> before;
				for (const auto& pak : g_paks)
					before.insert(pak->GetId());
				scriptError = FixScripts(paks, scriptError, file, before,
				                         [&](const std::string& mod, const std::string& why) {
					                         fail(paks, mod, why);
				                         });
				if (!scriptError.empty()) {
					// No mod to blame: put the mounted set back, and leave the user's
					// choice as it is.
					Swap(paks, g_paks);
					error = scriptError;
					SPLog("Mods not applied: %s", error.c_str());
					return ModApplyResult::Refused;
				}
			}

			g_paks = std::move(paks);
			g_mods = mods;
			helper->SetEnabledMods(g_mods);
			WriteCheckedPaks();
			SPLog("Mods applied without a restart");
			return ModApplyResult::Applied;
		}

		std::vector<ModFailure> ModOverlay::TakeStartupFailures() {
			std::vector<ModFailure> failures;
			failures.swap(g_failures);
			return failures;
		}

		std::string ModOverlay::Describe(const std::vector<ModFailure>& failures, bool disabled) {
			if (failures.empty())
				return {};
			std::string text = disabled
			                     ? _Tr("ModOverlay", "These mods failed to load and were disabled:")
			                     : _Tr("ModOverlay", "These mods failed to load:");
			for (const ModFailure& failure : failures)
				text += "\n- " + failure.mod + ": " + failure.reason;
			return text;
		}
	} // namespace gui
} // namespace spades
