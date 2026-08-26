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
	class ViewWeaponSpring {
		double position = 0;
		double desired = 0;
		double velocity = 0;
		double frequency = 1;
		double damping = 1;

		ViewWeaponSpring() {}

		ViewWeaponSpring(double f, double d) {
			frequency = f;
			damping = d;
		}

		ViewWeaponSpring(double f, double d, double des) {
			frequency = f;
			damping = d;
			desired = des;
		}

		void Update(double updateLength) {
			double timeStep = 1.0 / 240.0;

			// forces updates into at least 240 fps.
			for (double timeLeft = updateLength; timeLeft > 0; timeLeft -= timeStep) {
				double dt = Min(timeStep, timeLeft);
				double acceleration = (desired - position) * frequency;
				velocity = velocity + acceleration * dt;
				velocity -= velocity * damping * dt;
				position = position + velocity * dt;
			}
		}
	}

	class ViewWeaponEvent {
		bool activated = false;
		bool acknowledged = false;

		void Activate() {
			if (not acknowledged)
				activated = true;
		}

		bool WasActivated() {
			return acknowledged ? false : activated;
		}

		void Acknowledge() {
			acknowledged = true;
		}

		void Reset() {
			activated = false;
			acknowledged = false;
		}
	}

	class BasicViewWeapon : IToolSkin, IViewToolSkin, IWeaponSkin, IWeaponSkin2, IWeaponSkin3 {
		protected float time;

		// IToolSkin
		protected float sprintState;
		protected float raiseState;
		protected Vector3 teamColor;
		protected bool muted;

		float SprintState {
			set { sprintState = value; }
			get { return sprintState; }
		}

		float RaiseState {
			set { raiseState = value; }
			get { return raiseState; }
		}

		Vector3 TeamColor {
			set { teamColor = value; }
			get { return teamColor; }
		}

		bool IsMuted {
			set { muted = value; }
			get { return muted; }
		}

		// IWeaponSkin
		protected float aimDownSightState;
		protected float aimDownSightStateSmooth;
		protected float readyState;
		protected bool reloading;
		protected float reloadProgress;
		protected int ammo, clipSize;
		protected float localFireVibration;
		protected float sprintStateSmooth;

		float AimDownSightState {
			set {
				aimDownSightState = value;
				aimDownSightStateSmooth = SmoothStep(value);
			}
			get { return aimDownSightState; }
		}

		float AimDownSightStateSmooth {
			get { return aimDownSightStateSmooth; }
		}

		bool IsReloading {
			set { reloading = value; }
			get { return reloading; }
		}
		float ReloadProgress {
			set { reloadProgress = value; }
			get { return reloadProgress; }
		}
		int Ammo {
			set { ammo = value; }
			get { return ammo; }
		}
		int ClipSize {
			set { clipSize = value; }
			get { return clipSize; }
		}

		float ReadyState {
			set { readyState = value; }
			get { return readyState; }
		}

		// IViewToolSkin
		protected Matrix4 eyeMatrix;
		protected Vector3 swing;
		protected Vector3 leftHand;
		protected Vector3 rightHand;

		Matrix4 EyeMatrix {
			set { eyeMatrix = value; }
			get { return eyeMatrix; }
		}

		Vector3 Swing {
			set { swing = value; }
			get { return swing; }
		}

		Vector3 LeftHandPosition {
			set { leftHand = value; }
			get { return leftHand; }
		}
		Vector3 RightHandPosition {
			get { return rightHand; }
			set { rightHand = value; }
		}

		// IWeaponSkin2
		protected float environmentRoom;
		protected float environmentSize;

		void SetSoundEnvironment(float room, float size, float distance) {
			environmentRoom = room;
			environmentSize = size;
		}
		// set_SoundOrigin is not called for first-person skin scripts
		Vector3 SoundOrigin { set {} }

		// IWeaponSkin3
		Vector3 MuzzlePosition { get { return GetViewWeaponMatrix() * Vector3(0.0F, 0.35F, -0.05F); } }
		Vector3 CaseEjectPosition { get { return GetViewWeaponMatrix() * Vector3(0.0F, -0.1F, -0.05F); } }

		protected Renderer@ renderer;

		protected Model@ charmModel;
		protected Model@ charmBaseModel;

		protected Image@ sightImage;
		protected Image@ scopeImage;
		protected Image@ dotSightImage;
		protected Image@ reflexImage;
		protected Image@ ballImage;

		// Springs
		protected ViewWeaponSpring charmHorizontalSwingSpring;
		protected ViewWeaponSpring charmVerticalSwingSpring;
		protected ViewWeaponSpring charmSprintSpring;
		protected ViewWeaponSpring charmRaiseSpring;

		protected ConfigItem cg_fov("cg_fov");
		protected ConfigItem cg_reflexScope("cg_reflexScope", "0");
		protected ConfigItem cg_trueAimDownSight("cg_trueAimDownSight");
		protected ConfigItem cg_weaponCharms("cg_weaponCharms", "1");

		protected ConfigItem cg_viewWeaponX("cg_viewWeaponX");
		protected ConfigItem cg_viewWeaponY("cg_viewWeaponY");
		protected ConfigItem cg_viewWeaponZ("cg_viewWeaponZ");
		protected ConfigItem cg_viewWeaponSide("cg_viewWeaponSide");

		protected ConfigItem cg_target("cg_target", "1");
		protected ConfigItem cg_targetLines("cg_targetLines");
		protected ConfigItem cg_targetColor("cg_targetColor");
		protected ConfigItem cg_targetColorR("cg_targetColorR");
		protected ConfigItem cg_targetColorG("cg_targetColorG");
		protected ConfigItem cg_targetColorB("cg_targetColorB");
		protected ConfigItem cg_targetAlpha("cg_targetAlpha");
		protected ConfigItem cg_targetGap("cg_targetGap");
		protected ConfigItem cg_targetSizeHorizontal("cg_targetSizeHorizontal");
		protected ConfigItem cg_targetSizeVertical("cg_targetSizeVertical");
		protected ConfigItem cg_targetThickness("cg_targetThickness");
		protected ConfigItem cg_targetTStyle("cg_targetTStyle");
		protected ConfigItem cg_targetDot("cg_targetDot");
		protected ConfigItem cg_targetDotColorR("cg_targetDotColorR");
		protected ConfigItem cg_targetDotColorG("cg_targetDotColorG");
		protected ConfigItem cg_targetDotColorB("cg_targetDotColorB");
		protected ConfigItem cg_targetDotAlpha("cg_targetDotAlpha");
		protected ConfigItem cg_targetDotThickness("cg_targetDotThickness");
		protected ConfigItem cg_targetOutline("cg_targetOutline");
		protected ConfigItem cg_targetOutlineColorR("cg_targetOutlineColorR");
		protected ConfigItem cg_targetOutlineColorG("cg_targetOutlineColorG");
		protected ConfigItem cg_targetOutlineColorB("cg_targetOutlineColorB");
		protected ConfigItem cg_targetOutlineAlpha("cg_targetOutlineAlpha");
		protected ConfigItem cg_targetOutlineThickness("cg_targetOutlineThickness");
		protected ConfigItem cg_targetOutlineRoundedStyle("cg_targetOutlineRoundedStyle");
		protected ConfigItem cg_targetDynamic("cg_targetDynamic");
		protected ConfigItem cg_targetDynamicSplitDist("cg_targetDynamicSplitdist");

		protected ConfigItem cg_pngScope("cg_pngScope");
		protected ConfigItem cg_scopeLines("cg_scopeLines");
		protected ConfigItem cg_scopeColor("cg_scopeColor");
		protected ConfigItem cg_scopeColorR("cg_scopeColorR");
		protected ConfigItem cg_scopeColorG("cg_scopeColorG");
		protected ConfigItem cg_scopeColorB("cg_scopeColorB");
		protected ConfigItem cg_scopeAlpha("cg_scopeAlpha");
		protected ConfigItem cg_scopeGap("cg_scopeGap");
		protected ConfigItem cg_scopeSizeHorizontal("cg_scopeSizeHorizontal");
		protected ConfigItem cg_scopeSizeVertical("cg_scopeSizeVertical");
		protected ConfigItem cg_scopeThickness("cg_scopeThickness");
		protected ConfigItem cg_scopeTStyle("cg_scopeTStyle");
		protected ConfigItem cg_scopeDot("cg_scopeDot");
		protected ConfigItem cg_scopeDotColorR("cg_scopeDotColorR");
		protected ConfigItem cg_scopeDotColorG("cg_scopeDotColorG");
		protected ConfigItem cg_scopeDotColorB("cg_scopeDotColorB");
		protected ConfigItem cg_scopeDotAlpha("cg_scopeDotAlpha");
		protected ConfigItem cg_scopeDotThickness("cg_scopeDotThickness");
		protected ConfigItem cg_scopeOutline("cg_scopeOutline");
		protected ConfigItem cg_scopeOutlineColorR("cg_scopeOutlineColorR");
		protected ConfigItem cg_scopeOutlineColorG("cg_scopeOutlineColorG");
		protected ConfigItem cg_scopeOutlineColorB("cg_scopeOutlineColorB");
		protected ConfigItem cg_scopeOutlineAlpha("cg_scopeOutlineAlpha");
		protected ConfigItem cg_scopeOutlineThickness("cg_scopeOutlineThickness");
		protected ConfigItem cg_scopeOutlineRoundedStyle("cg_scopeOutlineRoundedStyle");
		protected ConfigItem cg_scopeDynamic("cg_scopeDynamic");
		protected ConfigItem cg_scopeDynamicSplitDist("cg_scopeDynamicSplitdist");

		BasicViewWeapon(Renderer@ renderer) {
			@this.renderer = renderer;
			@charmModel = renderer.RegisterModel("Models/Weapons/Charms/Charm.kv6");
			@charmBaseModel = renderer.RegisterModel("Models/Weapons/Charms/CharmBase.kv6");
			@scopeImage = renderer.RegisterImage("Gfx/Rifle.png");
			@sightImage = renderer.RegisterImage("Gfx/Sight.tga");
			@dotSightImage = renderer.RegisterImage("Gfx/DotSight.tga");
			@reflexImage = renderer.RegisterImage("Gfx/ReflexSight.png");
			@ballImage = renderer.RegisterImage("Gfx/Ball.png");

			time = -1.0F;
			localFireVibration = 0.0F;

			charmHorizontalSwingSpring = ViewWeaponSpring(200, 4);
			charmVerticalSwingSpring = ViewWeaponSpring(200, 4);
			charmSprintSpring = ViewWeaponSpring(200, 4);
			charmRaiseSpring = ViewWeaponSpring(200, 4);
		}

		float GetLocalFireVibration() { return localFireVibration; }
		float GetMotionGain() { return 1.0F - AimDownSightStateSmooth * 0.4F; }
		float GetZPos() { return 0.2F - AimDownSightStateSmooth * 0.05F; }
		float GetZPos(float sightPosZ) { return Mix(0.2F, sightPosZ, AimDownSightStateSmooth); }

		Vector3 GetLocalFireVibrationOffset() {
			float vib = GetLocalFireVibration();
			float motion = GetMotionGain();
			Vector3 hip(0.0F, 0.0F, 0.0F);
			hip.x = sin(vib * PiF * 2.0F) * 0.008F * motion;
			hip.y = vib * (vib - 1.0F) * 0.14F * motion;
			hip.z = vib * (1.0F - vib) * 0.03F * motion;
			Vector3 ads = Vector3(0.0F, vib * (vib - 1.0F) * vib * 0.3F * motion, 0.0F);
			return Mix(hip, ads, AimDownSightStateSmooth);
		}

		// creates a rotation matrix from euler angles (in the form of a Vector3) x-y-z
		Matrix4 CreateEulerAnglesMatrix(Vector3 angles) {
			Matrix4 mat;
			mat = CreateRotateMatrix(Vector3(1, 0, 0), angles.x);
			mat = CreateRotateMatrix(Vector3(0, 1, 0), angles.y) * mat;
			mat = CreateRotateMatrix(Vector3(0, 0, 1), angles.z) * mat;
			return mat;
		}

		// rotates gun matrix to ensure the sight is in the center of screen (0, ?, 0)
		Matrix4 AdjustToAlignSight(Matrix4 mat, Vector3 sightPos, float fade) {
			Vector3 p = mat * sightPos;
			mat = CreateRotateMatrix(Vector3(0, 1, 1), atan2(p.x, p.y) * fade) * mat;
			mat = CreateRotateMatrix(Vector3(-1, 0, 0), atan2(p.z, p.y) * fade) * mat;
			return mat;
		}

		Matrix4 GetViewWeaponMatrix() {
			Matrix4 mat;

			float weapSide = Clamp(cg_viewWeaponSide.FloatValue, -1.0F, 1.0F);
			bool leftHanded = weapSide < 0.0F;

			float sp = 1.0F - AimDownSightStateSmooth;

			// sprint animation
			if (sprintStateSmooth > 0.0F) {
				mat = CreateEulerAnglesMatrix(Vector3(0.3F, -0.1F, -0.55F) * sprintStateSmooth) * mat;
				mat = CreateTranslateMatrix(Vector3(0.23F, -0.05F, 0.15F) * sprintStateSmooth) * mat;
			}

			// raise gun animation
			if (raiseState < 1.0F) {
				float putdown = 1.0F - raiseState;
				mat = CreateRotateMatrix(Vector3(0, 0, -1), 1.3F * putdown) * mat;
				mat = CreateRotateMatrix(Vector3(0, 1, 0), 0.2F * putdown) * mat;
				mat = CreateTranslateMatrix(Vector3(0.1F, -0.3F, 0.1F) * putdown) * mat;
			}

			// recoil animation
			if (readyState < 1.0F) {
				float per = SmoothStep(1.0F - readyState);
				mat = CreateTranslateMatrix(Vector3(-0.25F * sp, -0.5F, 0.25F * sp) * 0.1F * per) * mat;
				mat = CreateRotateMatrix(Vector3(-1, 0, 0), (0.05F * per) * sp) * mat;
			}

			// add weapon offset
			Vector3 trans(0.0F, 0.0F, 0.0F);

			if (cg_trueAimDownSight.BoolValue and cg_pngScope.BoolValue)
				trans += Vector3(Mix(-0.26F, -0.13F, sp), 0.5F * sp, Mix(0.3F, 0.2F, sp));
			else
				trans += Vector3(-0.13F * sp, 0.5F, GetZPos());

			// manual adjustment
			trans.x += cg_viewWeaponX.FloatValue * sp;
			trans.y += cg_viewWeaponY.FloatValue * sp;
			trans.z += cg_viewWeaponZ.FloatValue * sp;
			trans.x *= abs(weapSide);

			// add weapon sway
			trans += swing * GetMotionGain();

			mat = CreateTranslateMatrix(trans) * mat;

			// mirror geometry when left-handed
			if (leftHanded)
				mat = CreateScaleMatrix(Vector3(-1.0F, 1.0F, 1.0F)) * mat;

			return mat;
		}

		void Update(float dt) {
			if (time < 0.0F)
				time = 0.0F;

			if (localFireVibration > 0.0F) {
				localFireVibration -= dt * 10.0F;
				if (localFireVibration < 0.0F)
					localFireVibration = 0.0F;
			}

			float sprintStateSS = sprintState * sprintState;
			if (sprintStateSS > sprintStateSmooth)
				sprintStateSmooth = Mix(sprintStateSmooth, sprintStateSS, 1.0F - pow(0.001F, dt));
			else
				sprintStateSmooth = sprintStateSS;

			// update charm springs
			charmHorizontalSwingSpring.velocity += swing.x * 60 * dt * 2;
			charmVerticalSwingSpring.velocity += swing.z * 60 * dt * 2;
			charmHorizontalSwingSpring.Update(dt);
			charmVerticalSwingSpring.Update(dt);
			charmSprintSpring.Update(dt);
			charmRaiseSpring.Update(dt);

			time += Min(dt, 0.05F);
		}

		void WeaponFired() {
			localFireVibration = 1.0F;
		}

		void AddToScene() {}
		void ReloadingWeapon() {}
		void ReloadedWeapon() {}

		void DrawReflexSight3D(Image@ img, Vector3 pos, float size, Vector3 color = Vector3(1, 0, 0)) {
			float reflexSize = size * 0.025F;
			float reflexOp = AimDownSightStateSmooth * 5.0F - 4.0F;
			Vector3 sightColor = color * reflexOp;

			renderer.ColorP = Vector4(sightColor.x, sightColor.y, sightColor.z, 0.0F);
			renderer.AddLongSprite(img, pos, pos, reflexSize);
			renderer.ColorP = Vector4(reflexOp, reflexOp, reflexOp, 0.0F); // premultiplied alpha
			renderer.AddLongSprite(img, pos, pos, reflexSize);
		}

		void DrawReflexSight2D(Matrix4 sightMat, float size = 0.02F) {
			float sw = renderer.ScreenWidth;
			float sh = renderer.ScreenHeight;

			bool dotReflex = cg_reflexScope.IntValue == 3;
			float reflexSize = dotReflex ? size * 0.125F : size;

			// scale sight according to the fov value
			float fov = tan(rad(cg_fov.FloatValue * 0.5F));
			reflexSize /= fov;
			reflexSize *= sh;

			// scale sight according to the distance from the eye to the sight
			Vector3 sightPos = sightMat.GetOrigin();
			float scale = 1.0F / sightPos.y;
			reflexSize *= scale;

			Vector2 scrCenter = Vector2(sw, sh) * 0.5F;
			Vector2 imgPos = scrCenter - (reflexSize * 0.5F);

			float reflexOp = AimDownSightStateSmooth * 5.0F - 4.0F;

			if (dotReflex) {
				renderer.ColorP = Vector4(reflexOp, 0.0F, 0.0F, 0.0F);
				renderer.DrawImage(ballImage, AABB2(imgPos.x, imgPos.y, reflexSize, reflexSize));
				renderer.ColorP = Vector4(reflexOp, reflexOp, reflexOp, 0.0F); // premultiplied alpha
				renderer.DrawImage(ballImage, AABB2(imgPos.x, imgPos.y, reflexSize, reflexSize));
			} else {
				renderer.ColorP = Vector4(reflexOp, reflexOp, reflexOp, 0.0F); // premultiplied alpha
				renderer.DrawImage(reflexImage, AABB2(imgPos.x, imgPos.y, reflexSize, reflexSize));
			}
		}

		void Draw2D() {
			float sw = renderer.ScreenWidth;
			float sh = renderer.ScreenHeight;

			Vector2 scrCenter = Vector2(sw, sh) * 0.5F;

			TargetParam param;
			int targetType = cg_target.IntValue;
			int scopeType = cg_pngScope.IntValue;

			Vector4 color = Vector4(1, 1, 1, 1);

			// draw scope
			if (AimDownSightStateSmooth > 0.99F) {
				if (scopeType > 0) {
					IntVector3 col;
					col.x = cg_scopeColorR.IntValue;
					col.y = cg_scopeColorG.IntValue;
					col.z = cg_scopeColorB.IntValue;

					color = ConvertColorRGBA(col);
					color.w = Clamp(cg_scopeAlpha.IntValue, 0, 255) / 255.0F;

					if (scopeType == 1) { // draw classic png scope
						Vector2 imgSize = Vector2(scopeImage.Width, scopeImage.Height);
						imgSize *= Max(1.0F, sw / 800.0F);
						imgSize *= Min(1.0F, sh / 600.0F);

						if (cg_scopeDynamic.BoolValue)
							imgSize *= Max(Mix(1.25F, 1.0F, readyState), 1.0F);

						Vector2 imgPos = scrCenter - (imgSize * 0.5F);

						renderer.ColorNP = Vector4(1.0F, 1.0F, 1.0F, color.w);
						renderer.DrawImage(scopeImage, AABB2(imgPos.x, imgPos.y, imgSize.x, imgSize.y));
					} else if (scopeType == 2) { // draw dot png scope
						Vector2 imgSize = Vector2(dotSightImage.Width, dotSightImage.Height);
						Vector2 imgPos = scrCenter - (imgSize * 0.5F);
						renderer.ColorNP = color;
						renderer.DrawImage(dotSightImage, imgPos);
					} else if (scopeType == 3) { // draw custom crosshair scope
						param.lineColor = color;
						param.drawLines = cg_scopeLines.BoolValue;
						param.useTStyle = cg_scopeTStyle.BoolValue;
						param.lineGap = cg_scopeGap.FloatValue;
						param.lineLength.x = cg_scopeSizeHorizontal.FloatValue;
						param.lineLength.y = cg_scopeSizeVertical.FloatValue;
						param.lineThickness = Max(1.0F, cg_scopeThickness.FloatValue);

						if (cg_scopeDynamic.BoolValue) {
							float maxDist = cg_scopeDynamicSplitDist.FloatValue;
							param.lineGap += localFireVibration * maxDist;
						}

						param.drawDot = cg_scopeDot.BoolValue;
						col.x = cg_scopeDotColorR.IntValue;
						col.y = cg_scopeDotColorG.IntValue;
						col.z = cg_scopeDotColorB.IntValue;
						color = ConvertColorRGBA(col);
						color.w = Clamp(cg_scopeDotAlpha.IntValue, 0, 255) / 255.0F;
						param.dotColor = color;
						param.dotThickness = Max(1.0F, cg_scopeDotThickness.FloatValue);

						param.drawOutline = cg_scopeOutline.BoolValue;
						param.useRoundedStyle = cg_scopeOutlineRoundedStyle.BoolValue;
						col.x = cg_scopeOutlineColorR.IntValue;
						col.y = cg_scopeOutlineColorG.IntValue;
						col.z = cg_scopeOutlineColorB.IntValue;
						color = ConvertColorRGBA(col);
						color.w = Clamp(cg_scopeOutlineAlpha.IntValue, 0, 255) / 255.0F;
						param.outlineColor = color;
						param.outlineThickness = Max(1.0F, cg_scopeOutlineThickness.FloatValue);

						DrawTarget(renderer, scrCenter, param);
					}
				}

				return; // do not draw the target when aiming
			}

			// draw target
			if (targetType > 0) {
				IntVector3 col;
				switch (cg_targetColor.IntValue) {
					case 1: col = IntVector3(250, 50, 50); break; // red
					case 2: col = IntVector3(50, 250, 50); break; // green
					case 3: col = IntVector3(50, 50, 250); break; // blue
					case 4: col = IntVector3(250, 250, 50); break; // yellow
					case 5: col = IntVector3(50, 250, 250); break; // cyan
					case 6: col = IntVector3(250, 50, 250); break; // pink
					default: // custom
						col.x = cg_targetColorR.IntValue;
						col.y = cg_targetColorG.IntValue;
						col.z = cg_targetColorB.IntValue;
						break;
				}

				color = ConvertColorRGBA(col);
				color.w = Clamp(cg_targetAlpha.IntValue, 0, 255) / 255.0F;

				if (targetType == 1) { // draw default target
					Vector2 imgSize = Vector2(sightImage.Width, sightImage.Height);
					Vector2 imgPos = scrCenter - (imgSize * 0.5F);
					renderer.ColorNP = color;
					renderer.DrawImage(sightImage, imgPos);
				} else if (targetType == 2) { // draw custom target
					param.lineColor = color;
					param.drawLines = cg_targetLines.BoolValue;
					param.useTStyle = cg_targetTStyle.BoolValue;
					param.lineGap = cg_targetGap.FloatValue;
					param.lineLength.x = cg_targetSizeHorizontal.FloatValue;
					param.lineLength.y = cg_targetSizeVertical.FloatValue;
					param.lineThickness = Max(1.0F, cg_targetThickness.FloatValue);

					if (cg_targetDynamic.BoolValue) {
						float maxDist = cg_targetDynamicSplitDist.FloatValue;
						param.lineGap += sprintState * maxDist;
						param.lineGap += localFireVibration * maxDist;
						param.lineGap += (1.0F - raiseState) * maxDist;
					}

					param.drawDot = cg_targetDot.BoolValue;
					col.x = cg_targetDotColorR.IntValue;
					col.y = cg_targetDotColorG.IntValue;
					col.z = cg_targetDotColorB.IntValue;
					color = ConvertColorRGBA(col);
					color.w = Clamp(cg_targetDotAlpha.IntValue, 0, 255) / 255.0F;
					param.dotColor = color;
					param.dotThickness = Max(1.0F, cg_targetDotThickness.FloatValue);

					param.drawOutline = cg_targetOutline.BoolValue;
					param.useRoundedStyle = cg_targetOutlineRoundedStyle.BoolValue;
					col.x = cg_targetOutlineColorR.IntValue;
					col.y = cg_targetOutlineColorG.IntValue;
					col.z = cg_targetOutlineColorB.IntValue;
					color = ConvertColorRGBA(col);
					color.w = Clamp(cg_targetOutlineAlpha.IntValue, 0, 255) / 255.0F;
					param.outlineColor = color;
					param.outlineThickness = Max(1.0F, cg_targetOutlineThickness.FloatValue);

					DrawTarget(renderer, scrCenter, param);
				}
			}
		}
	}
}