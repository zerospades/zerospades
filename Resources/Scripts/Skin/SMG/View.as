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
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

namespace spades {
	class ViewSMGSkin : BasicViewWeapon {
		private AudioDevice@ audioDevice;

		private Model@ gunModel;
		private Model@ magazineModel;
		private Model@ rearSightModel;
		private Model@ frontSightModel;
		private Model@ frontPinSightModel;
		private Model@ reflexSightModel;

		private AudioChunk@[] fireSounds(4);
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@[] fireSmallReverbSounds(4);
		private AudioChunk@[] fireLargeReverbSounds(4);
		private AudioChunk@ reloadSound;

		// Constants
		private Vector3 gunPivot = Vector3(1.0F, 16.5F, 3.5F);

		// Attachment Points
		private Vector3 magazineAttachment = Vector3(0.0F, 0.0F, 0.0F);
		private Vector3 rearSightAttachment = Vector3(0.04F, -9.0F, -4.9F);
		private Vector3 frontSightAttachment = Vector3(0.05F, 5.0F, -4.85F);
		private Vector3 frontPinSightAttachment = Vector3(0.025F, 4.85F, -4.875F);
		private Vector3 reflexSightAttachment = Vector3(0.04F, -7.825F, -5.875F);

		// Scales
		private float globalScale = 0.033F;
		private float magazineScale = 0.5F;
		private float rearSightScale = 0.08F;
		private float frontSightScale = 0.1F;
		private float frontPinSightScale = 0.05F;
		private float reflexSightScale = 0.08F;

		// A bunch of springs.
		private ViewWeaponSpring reloadPitchSpring = ViewWeaponSpring(150, 12);
		private ViewWeaponSpring reloadRollSpring = ViewWeaponSpring(150, 16);
		private ViewWeaponSpring reloadOffsetSpring = ViewWeaponSpring(150, 12);
		private ViewWeaponSpring recoilVerticalSpring = ViewWeaponSpring(300, 24);
		private ViewWeaponSpring recoilBackSpring = ViewWeaponSpring(300, 16);
		private ViewWeaponSpring recoilRotationSpring = ViewWeaponSpring(50, 8);
		private ViewWeaponSpring horizontalSwingSpring = ViewWeaponSpring(100, 12);
		private ViewWeaponSpring verticalSwingSpring = ViewWeaponSpring(100, 12);
		private ViewWeaponSpring sprintSpring = ViewWeaponSpring(100, 10);
		private ViewWeaponSpring raiseSpring = ViewWeaponSpring(200, 20, 1);

		// A bunch of events.
		private ViewWeaponEvent magazineTouched = ViewWeaponEvent();
		private ViewWeaponEvent magazineRemoved = ViewWeaponEvent();
		private ViewWeaponEvent magazineInserted = ViewWeaponEvent();
		private ViewWeaponEvent chargingHandlePulled = ViewWeaponEvent();

		// A bunch of states.
		private double lastSprintState = 0;
		private double lastRaiseState = 0;

		Matrix4 AdjustToReload(Matrix4 mat) {
			if (reloadProgress < 0.6) {
				reloadPitchSpring.desired = 0.6;
				reloadRollSpring.desired = 0.6;
				charmVerticalSwingSpring.desired = -0.01F;
			} else if (reloadProgress < 0.9) {
				reloadPitchSpring.desired = 0;
				reloadRollSpring.desired = -0.6;
				charmVerticalSwingSpring.desired = 0;
			} else {
				reloadPitchSpring.desired = 0;
				reloadRollSpring.desired = 0;
				charmVerticalSwingSpring.desired = 0;
			}

			if (magazineTouched.WasActivated()) {
				magazineTouched.Acknowledge();
				reloadPitchSpring.velocity = 4;
			}

			if (magazineRemoved.WasActivated()) {
				magazineRemoved.Acknowledge();
				reloadPitchSpring.velocity = -4;
				charmVerticalSwingSpring.velocity += 0.1F;
			}

			if (magazineInserted.WasActivated()) {
				magazineInserted.Acknowledge();
				reloadPitchSpring.velocity = 8;
				charmVerticalSwingSpring.velocity += 0.1F;
			}

			if (chargingHandlePulled.WasActivated()) {
				chargingHandlePulled.Acknowledge();
				reloadPitchSpring.velocity = 5;
				reloadOffsetSpring.velocity = 1;
				charmVerticalSwingSpring.velocity += 0.2F;
			}

			float sp = 1.0F - AimDownSightStateSmooth;

			mat *= CreateRotateMatrix(Vector3(0, 1, 0), 0.6F * reloadRollSpring.position * sp);
			mat *= CreateRotateMatrix(Vector3(-1, 0, 0), 0.25F * reloadPitchSpring.position * sp);
			mat *= CreateTranslateMatrix(Vector3(0.0F, -1.0F, 0.0F) * reloadOffsetSpring.position * sp);
			mat *= CreateTranslateMatrix(Vector3(0.0F, 0.0F, -globalScale) * reloadPitchSpring.position * sp);

			return mat;
		}

		Vector3 GetMagazineOffset() {
			Vector3 magazineDetachedOffset = Vector3(0.0F, 0.0F, 15.0F);

			if (reloadProgress < 0.2) {
				return magazineAttachment;
			} else if (reloadProgress < 0.25) {
				magazineRemoved.Activate();
				float per = Min(1.0, (reloadProgress-0.2) / (0.25-0.2));
				return Mix(magazineAttachment, magazineDetachedOffset, SmoothStep(per));
			} else if (reloadProgress < 0.4) {
				return magazineDetachedOffset;
			} else if (reloadProgress < 0.5) {
				float per = Min(1.0, (reloadProgress-0.4) / (0.5-0.4));
				return Mix(magazineDetachedOffset, magazineAttachment, SmoothStep(per));
			} else {
				magazineInserted.Activate();
				return magazineAttachment;
			}
		}

		Vector3 GetLeftHandOffset() {
			Vector3 leftHandOffset = Vector3(1.0F, 3.0F, 0.5F);
			Vector3 magazineOffset = GetMagazineOffset() + Vector3(0.0F, 0.0F, 4.0F);

			Vector3 chargingHandleOffset = Vector3(4.0F, 5.0F, 0.0F);
			Vector3 handlePullingOffset = chargingHandleOffset + Vector3(0.0F, -6.0F, 0.0F);

			if (reloadProgress < 0.1) {
				float per = Min(1.0, reloadProgress / 0.1);
				return Mix(leftHandOffset, magazineOffset, SmoothStep(per));
			} else if (reloadProgress < 0.6) {
				magazineTouched.Activate();
				return magazineOffset;
			} else if (reloadProgress < 0.8) {
				float per = Min(1.0, (reloadProgress-0.6) / (0.8-0.6));
				return Mix(magazineOffset, chargingHandleOffset, SmoothStep(per));
			} else if (reloadProgress < 0.82) {
				return chargingHandleOffset;
			} else if (reloadProgress < 0.87) {
				chargingHandlePulled.Activate();
				float per = Min(1.0, (reloadProgress-0.82) / (0.87-0.82));
				return Mix(chargingHandleOffset, handlePullingOffset, SmoothStep(per));
			} else if (reloadProgress < 1.0) {
				float per = Min(1.0, (reloadProgress-0.87) / (1.0-0.87));
				return Mix(handlePullingOffset, leftHandOffset, SmoothStep(per));
			} else {
				return leftHandOffset;
			}
		}

		Vector3 GetRightHandOffset() {
			Vector3 rightHandOffset = Vector3(0.0F, -8.0F, 2.0F);

			// sprint animation
			rightHandOffset -= Vector3(0.0F, 3.0F, 0.5F) * sprintSpring.position;

			return rightHandOffset;
		}

		ViewSMGSkin(Renderer@ r, AudioDevice@ dev) {
			super(r);
			@audioDevice = dev;

			// load models
			@gunModel = renderer.RegisterModel("Models/Weapons/SMG/WeaponNoMagazine.kv6");
			@magazineModel = renderer.RegisterModel("Models/Weapons/SMG/Magazine.kv6");
			@rearSightModel = renderer.RegisterModel("Models/Weapons/SMG/SightRear.kv6");
			@frontSightModel = renderer.RegisterModel("Models/Weapons/SMG/SightFront.kv6");
			@frontPinSightModel = renderer.RegisterModel("Models/Weapons/SMG/SightFrontPin.kv6");
			@reflexSightModel = renderer.RegisterModel("Models/Weapons/SMG/SightReflex.kv6");

			// load sounds
			@fireSounds[0] = dev.RegisterSound("Sounds/Weapons/SMG/V2Local1.opus");
			@fireSounds[1] = dev.RegisterSound("Sounds/Weapons/SMG/V2Local2.opus");
			@fireSounds[2] = dev.RegisterSound("Sounds/Weapons/SMG/V2Local3.opus");
			@fireSounds[3] = dev.RegisterSound("Sounds/Weapons/SMG/V2Local4.opus");
			@fireFarSound = dev.RegisterSound("Sounds/Weapons/SMG/FireFar.opus");
			@fireStereoSound = dev.RegisterSound("Sounds/Weapons/SMG/FireStereo.opus");
			@fireSmallReverbSounds[0] = dev.RegisterSound("Sounds/Weapons/SMG/V2AmbienceSmall1.opus");
			@fireSmallReverbSounds[1] = dev.RegisterSound("Sounds/Weapons/SMG/V2AmbienceSmall2.opus");
			@fireSmallReverbSounds[2] = dev.RegisterSound("Sounds/Weapons/SMG/V2AmbienceSmall3.opus");
			@fireSmallReverbSounds[3] = dev.RegisterSound("Sounds/Weapons/SMG/V2AmbienceSmall4.opus");
			@fireLargeReverbSounds[0] = dev.RegisterSound("Sounds/Weapons/SMG/V2AmbienceLarge1.opus");
			@fireLargeReverbSounds[1] = dev.RegisterSound("Sounds/Weapons/SMG/V2AmbienceLarge2.opus");
			@fireLargeReverbSounds[2] = dev.RegisterSound("Sounds/Weapons/SMG/V2AmbienceLarge3.opus");
			@fireLargeReverbSounds[3] = dev.RegisterSound("Sounds/Weapons/SMG/V2AmbienceLarge4.opus");
			@reloadSound = dev.RegisterSound("Sounds/Weapons/SMG/ReloadLocal.opus");

			// load images
			@scopeImage = renderer.RegisterImage("Gfx/SMG.png");

			// initial raise spring
			raiseSpring.position = 1;
			charmRaiseSpring.position = 1;
		}

		void Update(float dt) {
			bool isSprinting = sprintState >= 1 or sprintState > lastSprintState;
			bool isRaised = raiseState >= 1 or raiseState > lastRaiseState;
			sprintSpring.desired = isSprinting ? 1 : 0;
			raiseSpring.desired = isRaised ? 0 : 1;
			charmSprintSpring.desired = isSprinting ? 1 : 0;
			charmRaiseSpring.desired = isRaised ? 0 : 1;
			lastSprintState = sprintState;
			lastRaiseState = raiseState;
			
			// update base script
			BasicViewWeapon::Update(dt);

			sprintSpring.Update(dt);
			raiseSpring.Update(dt);

			reloadPitchSpring.Update(dt);
			reloadRollSpring.Update(dt);
			reloadOffsetSpring.Update(dt);

			// smoothly increase recoil damping while aiming down sights
			recoilVerticalSpring.damping = Mix(16, 24, AimDownSightStateSmooth);
			recoilBackSpring.damping = Mix(24, 20, AimDownSightStateSmooth);
			recoilRotationSpring.damping = Mix(8, 16, AimDownSightStateSmooth);
			recoilVerticalSpring.Update(dt);
			recoilBackSpring.Update(dt);
			recoilRotationSpring.Update(dt);

			horizontalSwingSpring.velocity += swing.x * 60 * dt * 2;
			verticalSwingSpring.velocity += swing.z * 60 * dt * 2;
			horizontalSwingSpring.Update(dt);
			verticalSwingSpring.Update(dt);
		}

		void WeaponFired() {
			BasicViewWeapon::WeaponFired();

			if (!IsMuted) {
				Vector3 origin = Vector3(0.4F, -0.3F, 0.5F);
				AudioParam param;
				param.volume = 8.0F;
				audioDevice.PlayLocal(fireSounds[GetRandom(fireSounds.length)], origin, param);

				param.volume = 8.0F * environmentRoom;
				audioDevice.PlayLocal((environmentSize < 0.5F)
					? fireSmallReverbSounds[GetRandom(fireSmallReverbSounds.length)]
					: fireLargeReverbSounds[GetRandom(fireLargeReverbSounds.length)],
					origin, param);

				param.referenceDistance = 4.0F;
				param.volume = 1.0F;
				audioDevice.PlayLocal(fireFarSound, origin, param);
				param.referenceDistance = 1.0F;
				audioDevice.PlayLocal(fireStereoSound, origin, param);
			}

			recoilVerticalSpring.velocity += 0.75;
			recoilBackSpring.position += 0.1;
			recoilRotationSpring.velocity += (GetRandom() * 2 - 1);

			charmHorizontalSwingSpring.velocity += (GetRandom() * 2 - 1) * 0.1F;
			charmVerticalSwingSpring.velocity += 0.3F;
		}

		void ReloadingWeapon() {
			magazineTouched.Reset();
			magazineRemoved.Reset();
			magazineInserted.Reset();
			chargingHandlePulled.Reset();

			if (!IsMuted) {
				Vector3 origin = Vector3(0.4F, -0.3F, 0.5F);
				AudioParam param;
				param.volume = 0.2F;
				audioDevice.PlayLocal(reloadSound, origin, param);
			}

			charmHorizontalSwingSpring.velocity += 0.1F;
			charmVerticalSwingSpring.velocity += 0.1F;
		}

		// (override BasicViewWeapon::GetViewWeaponMatrix())
		Matrix4 GetViewWeaponMatrix() {
			Matrix4 mat;

			float weapSide = Clamp(cg_viewWeaponSide.FloatValue, -1.0F, 1.0F);
			bool leftHanded = weapSide < 0.0F;

			float sp = 1.0F - AimDownSightStateSmooth;

			// sprint animation
			mat = CreateEulerAnglesMatrix(Vector3(0.3F, -0.1F, -0.55F) * sprintSpring.position * sp) * mat;
			mat = CreateTranslateMatrix(Vector3(0.23F, -0.05F, 0.15F) * sprintSpring.position * sp) * mat;

			// raise gun animation
			mat = CreateRotateMatrix(Vector3(0, 0, -1), 1.3F * raiseSpring.position * sp) * mat;
			mat = CreateRotateMatrix(Vector3(0, 1, 0), 0.2F * raiseSpring.position * sp) * mat;
			mat = CreateTranslateMatrix(Vector3(0.1F, -0.3F, 0.1F) * raiseSpring.position * sp) * mat;

			// recoil animation
			Vector3 recoilRot(0.0F, 0.0F, 0.0F);
			recoilRot.x = -1.0F * recoilVerticalSpring.position;
			recoilRot.y = 0.2F * recoilRotationSpring.position;
			recoilRot.z = 0.2F * recoilRotationSpring.position;
			Vector3 recoilOffset = Vector3(0.0F, 0.0F, 0.0F);
			recoilOffset.y -= Mix(0.5F, 0.25F, sp) * recoilBackSpring.position;
			recoilOffset.z -= 0.1F * recoilVerticalSpring.position * sp;
			mat = CreateEulerAnglesMatrix(recoilRot * sp) * mat;
			mat = mat * CreateTranslateMatrix(recoilOffset);

			// add weapon offset
			Vector3 trans(0.0F, 0.0F, 0.0F);

			bool reflexScope = cg_reflexScope.BoolValue;
			Vector3 sightAttachment = reflexScope ? reflexSightAttachment : frontPinSightAttachment;
			Vector3 offset = reflexScope ? Vector3(sightAttachment.x, 0.0F, -0.05F) : Vector3(sightAttachment.x, 0.0F, 0.05F);
			Vector3 sightPos = (sightAttachment - offset) * globalScale;
			float sightZ = reflexScope ? (sightPos.z + 0.0125F) : sightPos.z;

			if (cg_trueAimDownSight.BoolValue and cg_pngScope.BoolValue)
				trans += Vector3(Mix(-0.26F, -0.13F, sp), 0.5F * sp, Mix(0.3F, 0.2F, sp));
			else
				trans += Vector3(-0.13F * sp, 0.5F, GetZPos(-sightZ));

			// manual adjustment
			trans.x += cg_viewWeaponX.FloatValue * sp;
			trans.y += cg_viewWeaponY.FloatValue * sp;
			trans.z += cg_viewWeaponZ.FloatValue * sp;
			trans.x *= abs(weapSide);

			// add weapon sway
			trans += swing * sp;

			mat = CreateTranslateMatrix(trans) * mat;

			// twist the gun when strafing
			Vector3 swingRot(0.0F, 0.0F, 0.0F);
			swingRot.z += 2.0F * horizontalSwingSpring.position;
			swingRot.x -= 2.0F * verticalSwingSpring.position;
			mat = mat * CreateEulerAnglesMatrix(swingRot * sp);

			if ((cg_trueAimDownSight.BoolValue and not cg_pngScope.BoolValue) and AimDownSightStateSmooth > 0.0F)
				mat = AdjustToAlignSight(mat, sightPos, AimDownSightStateSmooth);

			// reload animation
			mat = AdjustToReload(mat);

			// mirror geometry when left-handed
			if (leftHanded)
				mat = CreateScaleMatrix(Vector3(-1.0F, 1.0F, 1.0F)) * mat;

			return mat;
		}

		// IWeaponSkin3 (override BasicViewWeapon::{get_MuzzlePosition, get_CaseEjectPosition})
		Vector3 MuzzlePosition { get { return GetViewWeaponMatrix() * Vector3(0.0F, 0.25F, -0.075F); } }
		Vector3 CaseEjectPosition { get { return GetViewWeaponMatrix() * Vector3(-0.025F, -0.175F, -0.1F); } }

		void Draw2D() {
			// draw reflex sight (2D image)
			if (cg_reflexScope.IntValue >= 3
				and not cg_pngScope.BoolValue
				and AimDownSightStateSmooth > 0.8F) {
				DrawReflexSight2D(GetViewWeaponMatrix());
				return;
			}

			BasicViewWeapon::Draw2D();
		}

		void AddToScene() {
			if (cg_pngScope.BoolValue and AimDownSightStateSmooth > 0.99F) {
				LeftHandPosition = Vector3(0.0F, 0.0F, 0.0F);
				RightHandPosition = Vector3(0.0F, 0.0F, 0.0F);
				return;
			}

			Matrix4 mat = GetViewWeaponMatrix()
				* CreateScaleMatrix(globalScale);

			Vector3 leftHand, rightHand;
			leftHand = mat * GetLeftHandOffset();
			rightHand = mat * GetRightHandOffset();

			ModelRenderParam param;
			param.depthHack = true;

			Matrix4 weapMatrix = eyeMatrix * mat;

			// draw weapon
			param.matrix = weapMatrix;
			renderer.AddModel(gunModel, param);

			// draw magazine
			param.matrix = weapMatrix
				* CreateTranslateMatrix(GetMagazineOffset());
			renderer.AddModel(magazineModel, param);

			// draw sights
			if (cg_reflexScope.BoolValue) {
				param.matrix = weapMatrix
					* CreateTranslateMatrix(reflexSightAttachment)
					* CreateScaleMatrix(reflexSightScale);
				renderer.AddModel(reflexSightModel, param); // reflex
			} else {
				param.matrix = weapMatrix
					* CreateTranslateMatrix(rearSightAttachment)
					* CreateScaleMatrix(rearSightScale);
				renderer.AddModel(rearSightModel, param); // rear
			}

			param.matrix = weapMatrix
				* CreateTranslateMatrix(frontPinSightAttachment)
				* CreateScaleMatrix(frontPinSightScale);
			renderer.AddModel(frontPinSightModel, param); // front pin (emissive)

			param.matrix = weapMatrix
				* CreateTranslateMatrix(frontSightAttachment)
				* CreateScaleMatrix(frontSightScale);
			renderer.AddModel(frontSightModel, param); // front

			// draw charms
			if (cg_weaponCharms.BoolValue) {
				Vector3 charmRot(0.0F, 0.0F, 0.0F);

				// add sway spring
				charmRot.x -= 50.0F * charmVerticalSwingSpring.position;	// pitch
				charmRot.y += 30.0F * charmHorizontalSwingSpring.position;	// yaw
				charmRot.z += 20.0F * charmHorizontalSwingSpring.position;	// roll

				// add sprint/raise spring
				charmRot += Vector3(-0.5F, -0.2F, -0.2F) * charmSprintSpring.position;
				charmRot += Vector3(0.7F, 0.5F, 0.1F) * charmRaiseSpring.position;

				// tilt backward/forward based on camera pitch
				Vector3 front = eyeMatrix.GetAxis(1);
				charmRot.x -= asin(front.z) * 0.85F;

				// avoid clipping into the weapon
				charmRot.y = abs(charmRot.y);

				// rainbow color
				param.customColor = HSV(time * 0.05F, 0.5F, 1.0F);

				// draw base
				param.matrix = weapMatrix
					* CreateTranslateMatrix(1.95F, -2.5F, -1.8F)
					* CreateScaleMatrix(0.1F);
				renderer.AddModel(charmBaseModel, param);

				param.matrix = param.matrix
					* CreateTranslateMatrix(-0.75F, -1.0F, 1.25F)
					* CreateEulerAnglesMatrix(charmRot)
					* CreateEulerAnglesMatrix(Vector3(rad(90.0F), 0, rad(-90.0F)));
				renderer.AddModel(charmModel, param);
			}

			// draw reflex sight (3D sprite)
			int reflexMode = cg_reflexScope.IntValue;
			if ((reflexMode >= 1 and reflexMode < 3) and not cg_pngScope.BoolValue and AimDownSightStateSmooth > 0.8F) {
				bool dotReflex = reflexMode == 1;
				float reflexSize = dotReflex ? 0.02F : 0.125F;
				Vector3 sightPos = reflexSightAttachment - Vector3(reflexSightAttachment.x, 0.0F, -0.05F);
				DrawReflexSight3D(dotReflex ? ballImage : reflexImage, weapMatrix * sightPos, reflexSize);
			}

			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
		}
	}

	IWeaponSkin@ CreateViewSMGSkin(Renderer@ r, AudioDevice@ dev) {
		return ViewSMGSkin(r, dev);
	}
}
