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
	class BasicViewTool : IToolSkin, IViewToolSkin {
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

		Vector3 LeftHandPosition { get { return leftHand; } }
		Vector3 RightHandPosition { get { return rightHand; } }

		protected Renderer@ renderer;
		protected AudioDevice@ audioDevice;
		protected Image@ sightImage;

		// Springs
		protected ViewWeaponSpring horizontalSwingSpring = ViewWeaponSpring(100, 12);
		protected ViewWeaponSpring verticalSwingSpring = ViewWeaponSpring(100, 12);
		protected ViewWeaponSpring sprintSpring = ViewWeaponSpring(100, 10);
		protected ViewWeaponSpring raiseSpring = ViewWeaponSpring(200, 20, 1);

		private float lastSprintState = 0.0F;
		private float lastRaiseState = 0.0F;

		protected ConfigItem cg_viewWeaponX("cg_viewWeaponX");
		protected ConfigItem cg_viewWeaponY("cg_viewWeaponY");
		protected ConfigItem cg_viewWeaponZ("cg_viewWeaponZ");
		protected ConfigItem cg_viewWeaponSide("cg_viewWeaponSide");

		BasicViewTool(Renderer@ r, AudioDevice@ dev) {
			@renderer = r;
			@audioDevice = dev;
			@sightImage = renderer.RegisterImage("Gfx/Sight.tga");

			time = -1.0F;

			// initial raise spring
			raiseSpring.position = 1;
		}

		void Update(float dt) {
			if (time < 0.0F)
				time = 0.0F;
			time += Min(dt, 0.05F);

			bool isSprinting = sprintState >= 1 or sprintState > lastSprintState;
			bool isRaised = raiseState >= 1 or raiseState > lastRaiseState;
			sprintSpring.desired = isSprinting ? 1 : 0;
			raiseSpring.desired = isRaised ? 0 : 1;
			lastSprintState = sprintState;
			lastRaiseState = raiseState;

			horizontalSwingSpring.velocity += swing.x * 60 * dt * 2;
			verticalSwingSpring.velocity += swing.z * 60 * dt * 2;
			horizontalSwingSpring.Update(dt);
			verticalSwingSpring.Update(dt);

			sprintSpring.Update(dt);
			raiseSpring.Update(dt);
		}

		void AddToScene() {}

		float GetWeaponSide() { return Clamp(cg_viewWeaponSide.FloatValue, -1.0F, 1.0F); }

		// Adds the user adjustments (cg_viewWeapon*) and the sway to a base offset.
		Vector3 GetViewOffset(Vector3 base) {
			float weapSide = GetWeaponSide();

			Vector3 trans = base;

			// manual adjustment
			trans.x += cg_viewWeaponX.FloatValue;
			trans.y += cg_viewWeaponY.FloatValue;
			trans.z += cg_viewWeaponZ.FloatValue;
			trans.x *= abs(weapSide);

			// add weapon sway
			trans += swing;

			return trans;
		}

		// Twists the tool when strafing
		Matrix4 ApplySwingTwist(Matrix4 mat) {
			Vector3 swingRot(0.0F, 0.0F, 0.0F);
			swingRot.z += 2.0F * horizontalSwingSpring.position;
			swingRot.x -= 2.0F * verticalSwingSpring.position;
			return mat * CreateEulerAnglesMatrix(swingRot);
		}

		void Draw2D() {
			renderer.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 1.0F);
			renderer.DrawImage(sightImage,
				Vector2((renderer.ScreenWidth - sightImage.Width) * 0.5F,
						(renderer.ScreenHeight - sightImage.Height) * 0.5F));
		}
	}
}
