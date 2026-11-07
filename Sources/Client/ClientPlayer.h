/*
 Copyright (c) 2013 yvt

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

#pragma once

#include <array>
#include <string>

#include "Player.h"
#include <Core/Math.h>
#include <Core/RefCountedObject.h>
#include <ScriptBindings/ScriptManager.h>

namespace spades {
	class ScriptFunction;

	namespace client {
		class Client;
		class IRenderer;
		class IAudioDevice;
		class SandboxedRenderer;
		class Skin;

		// TODO: Use `shared_ptr` instead of `RefCountedObject`
		/** Representation of player which is used by
		 * drawing/view layer of game client. */
		class ClientPlayer : public RefCountedObject {
			Client& client;
			Player& player;
			/** The audio tag of this player's reload sounds. */
			int reloadSoundTag;

			float sprintState;
			float aimDownState;
			float toolRaiseState;
			Player::ToolType currentTool;
			float time;
			bool wasAimingDownSight;

			/**
			 * Indicates whether the third-person weapon skin script has the latest
			 * value of `OriginMatrix`.
			 */
			bool hasValidOriginMatrix;

			Vector3 viewWeaponOffset;
			Vector3 lastFront;
			Vector3 flashlightOrientation;
			Vector3 classicViewWeaponOrigin;

			asIScriptObject* spadeSkin;
			asIScriptObject* blockSkin;
			asIScriptObject* weaponSkin;
			asIScriptObject* grenadeSkin;

			asIScriptObject* spadeViewSkin;
			asIScriptObject* blockViewSkin;
			asIScriptObject* weaponViewSkin;
			asIScriptObject* grenadeViewSkin;

			// What the player's assets load through: the skin they spawned with, if any.
			Handle<IRenderer> viewRenderer;
			Handle<IAudioDevice> viewAudio;

			asIScriptObject* GetCurrentSkin(bool viewSkin);
			/**
			 * Builds the skins, from `skin` if there is one, loading through
			 * `renderer` and `audio`. Raises if a skin script does, having released
			 * what it made.
			 */
			void CreateSkins(Skin* skin, Handle<IRenderer> renderer, Handle<IAudioDevice> audio);
			void MakeSkins(const std::string& module, IRenderer& renderer, IAudioDevice& audio);

			Handle<SandboxedRenderer> sandboxedRenderer;

			std::array<Vector3, 3> GetFlashlightAxes();

			/**
			 * Get the base path of the player model set to use for this player.
			 * Zombies use a dedicated model set, everyone else uses the regular one.
			 */
			std::string GetModelPath();

			void AddToSceneThirdPersonView();
			void AddToSceneFirstPersonView();

			void SetSkinParameterForTool(Player::ToolType, asIScriptObject*);
			void SetCommonSkinParameter(asIScriptObject*);

			struct AmbienceInfo;
			AmbienceInfo ComputeAmbience();

			// TODO: Naming convention violation
			asIScriptObject* initScriptFactory(ScriptFunction& creator, const std::string& module,
				IRenderer& renderer, IAudioDevice& audio);

		protected:
			~ClientPlayer();

		public:
			ClientPlayer(Player& p, Client&);
			Player& GetPlayer() const { return player; }
			/** The renderer the player's assets load through, skin included. */
			IRenderer& GetRenderer() { return *viewRenderer; }

			void Update(float dt);
			void AddToScene();
			void Draw2D();

			bool IsChangingTool();
			void FiredWeapon();
			void EjectedBrass();
			void ReloadingWeapon();
			void ReloadedWeapon();
			/** Tells the skin how far through its animation the reload was, then
			 * stops the sounds the skin started for it unless the skin keeps them. */
			void ReloadAborted(float progress);

			float GetSprintState() { return sprintState; }

			/** 0 = normal, 1 = aiming down the sight. */
			float GetAimDownState() { return aimDownState; }

			bool ShouldRenderInThirdPersonView();

			/**
			 * Get the world coordinates representing the position of the currently wielded weapon's
			 * muzzle.
			 */
			Vector3 GetMuzzlePosition();

			/**
			 * Get the world coordinates representing the position of the currently wielded weapon's
			 * muzzle, based on the player's first-person view rendering.
			 */
			Vector3 GetMuzzlePositionInFirstPersonView();

			/**
			 * Get the world coordinates representing the position of the currently wielded weapon's
			 * case ejection port.
			 */
			Vector3 GetCaseEjectPosition();

			/**
			 * Get the world coordinates representing the position of the currently wielded weapon's
			 * case ejection port, based on the player's first-person view rendering.
			 */
			Vector3 GetCaseEjectPositionInFirstPersonView();

			Matrix4 GetEyeMatrix();
		};
	} // namespace client
} // namespace spades