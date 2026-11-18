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
	class ViewSpadeSkin : BasicViewTool, ISpadeSkin {
		private SpadeActionType actionType;
		private float actionProgress;

		SpadeActionType ActionType { set { actionType = value; } }
		float ActionProgress { set { actionProgress = value; } }

		private Model@ spadeModel;
		private Model@ pickaxeModel;
		private Model@ model;

		// The keyframed action poses are fed through springs (the rifle does the
		// same with its reload springs). Raise the frequency to reduce the lag,
		// lower the damping for more bounce.
		private ViewWeaponSpring bashSpring = ViewWeaponSpring(600, 26);
		private ViewWeaponSpring digSpring = ViewWeaponSpring(600, 26);
		private ViewWeaponSpring digSideSpring = ViewWeaponSpring(600, 26);

		private bool wasBashing = false;
		private float lastActionProgress = 0.0F;

		ViewSpadeSkin(Renderer@ r, AudioDevice@ dev) {
			super(r, dev);

			@spadeModel = renderer.RegisterModel("Models/Weapons/Spade/Spade.kv6");
			@pickaxeModel = renderer.RegisterModel("Models/Weapons/Spade/Pickaxe.kv6");
			@model = @spadeModel;
		}

		void Update(float dt) {
			float bashTarget = 0.0F;
			float digTarget = 0.0F;
			float digSideTarget = 0.0F;

			bool bashing = actionType == spades::SpadeActionType::Bash;
			if (bashing) {
				@model = @pickaxeModel;
				bashTarget = SmoothStep(1.0F - actionProgress);

				// The hit happens when the cooldown restarts: either the action just
				// switched to Bash or the progress dropped back to the start of a new
				// cycle. Snap the spring to the impact pose so the hit is not delayed
				// by the spring, then let it follow the recovery.
				if (not wasBashing or actionProgress < lastActionProgress) {
					bashSpring.position = bashTarget;
					bashSpring.velocity = 0;
				}
			} else if (actionType == spades::SpadeActionType::DigStart
				or actionType == spades::SpadeActionType::Dig) {
				@model = @spadeModel;
				float f = SmoothStep(1.0F - actionProgress);
				float f2;
				if (f >= 0.6F) {
					f2 = 0.0F;
					f = 1.0F - f;
				} else if (f >= 0.3F) {
					f2 = 0.6F - f;
					f = 0.4F;
				} else if (f >= 0.1F) {
					f2 = 0.3F;
					f = 0.4F;
				} else {
					f2 = f * 3.0F;
					f *= 4.0F;
				}
				digTarget = f;
				digSideTarget = f2;
			}
			bashSpring.desired = bashTarget;
			digSpring.desired = digTarget;
			digSideSpring.desired = digSideTarget;
			wasBashing = bashing;
			lastActionProgress = actionProgress;
			
			// update base script
			BasicViewTool::Update(dt);
			
			bashSpring.Update(dt);
			digSpring.Update(dt);
			digSideSpring.Update(dt);
		}

		void AddToScene() {
			Matrix4 mat = CreateScaleMatrix(0.033F);

			float weapSide = GetWeaponSide();
			bool leftHanded = weapSide < 0.0F;

			// sprint and raise animation
			float sprint = sprintSpring.position;
			float putdown = raiseSpring.position;
			float per = sprint + putdown - sprint * putdown;
			mat = CreateRotateMatrix(Vector3(0.0F, 1.0F, 0.0F), per * 1.3F) * mat;
			mat = CreateTranslateMatrix(Vector3(0.3F, -0.4F, -0.1F) * per) * mat;
			mat = CreateTranslateMatrix(0.0F, putdown * -0.3F, 0.0F) * mat;

			// bash animation
			float bash = bashSpring.position;
			mat = CreateRotateMatrix(Vector3(1.0F, 0.0F, 0.0F), bash * 1.7F) * mat;
			mat = CreateTranslateMatrix(bash * 0.2F, bash * 0.3F, 0.0F) * mat;

			// dig animation
			float f = digSpring.position;
			float f2 = digSideSpring.position;
			mat = CreateTranslateMatrix(Vector3(f2, f * -0.2F, -f2 * 0.25F)) * mat;
			mat = CreateRotateMatrix(Vector3(1.0F, 0.0F, 0.0F), f / 0.32F) * mat;
			mat = CreateRotateMatrix(Vector3(0.0F, 1.0F, 0.0F), -f) * mat;

			// weapon offset, manual adjustment and sway
			Vector3 trans = GetViewOffset(Vector3(-0.3F, 0.7F, 0.3F));
			mat = CreateTranslateMatrix(trans) * mat;

			// twist the tool when strafing
			mat = ApplySwingTwist(mat);

			// apply handedness
			if (leftHanded)
				mat = CreateScaleMatrix(Vector3(-1.0F, 1.0F, 1.0F)) * mat;

			// hands offset
			leftHand = mat * Vector3(0.0F, 0.0F, 7.0F);
			rightHand = mat * Vector3(0.0F, 0.0F, -2.0F);

			ModelRenderParam param;
			param.depthHack = true;
			param.matrix = eyeMatrix * mat;
			renderer.AddModel(model, param);
		}
	}

	ISpadeSkin@ CreateViewSpadeSkin(Renderer@ r, AudioDevice@ dev) {
		return ViewSpadeSkin(r, dev);
	}
}
