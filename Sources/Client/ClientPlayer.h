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

		// TODO: Use `shared_ptr` instead of `RefCountedObject`
		/** Representation of player which is used by
		 * drawing/view layer of game client. */
		class ClientPlayer : public RefCountedObject {
			Client& client;
			Player& player;

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

			asIScriptObject* GetCurrentSkin(bool viewSkin);

			Handle<SandboxedRenderer> sandboxedRenderer;

			std::array<Vector3, 3> GetFlashlightAxes();

			/**
			 * Whether a lamp at `lightOrigin` sits inside a solid voxel, in which case
			 * it would light the far side of that block, since spotlights aren't
			 * shadowed. Only meaningful for a lamp the camera isn't standing at.
			 */
			bool IsLampBuried(const Vector3& lightOrigin);

			/**
			 * Emit this player's flashlight from `lightOrigin`, if it should be lit
			 * at all. Shared by the first- and third-person paths, which differ only
			 * in where the lamp sits.
			 */
			void AddFlashlightToScene(const Vector3& lightOrigin);

			void AddToSceneThirdPersonView();
			void AddToSceneFirstPersonView();

			void SetSkinParameterForTool(Player::ToolType, asIScriptObject*);
			void SetCommonSkinParameter(asIScriptObject*);

			struct AmbienceInfo;
			AmbienceInfo ComputeAmbience();

			// TODO: Naming convention violation
			asIScriptObject* initScriptFactory(ScriptFunction& creator, 
				IRenderer& renderer, IAudioDevice& audio);

		protected:
			~ClientPlayer();

		public:
			ClientPlayer(Player& p, Client&);
			Player& GetPlayer() const { return player; }

			void Update(float dt);
			void AddToScene();
			void Draw2D();

			bool IsChangingTool();
			void FiredWeapon();
			void EjectedBrass();
			void ReloadingWeapon();
			void ReloadedWeapon();

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