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
	class ViewGrenadeSkin : BasicViewTool, IGrenadeSkin {
		private float cookTime;
		private float readyState;

		float CookTime { set { cookTime = value; } }
		float ReadyState { set { readyState = value; } }

		private Model@ model;

		ViewGrenadeSkin(Renderer@ r, AudioDevice@ dev) {
			super(r, dev);

			@model = renderer.RegisterModel("Models/Weapons/Grenade/Grenade.kv6");
		}

		void AddToScene() {
			Matrix4 mat = CreateScaleMatrix(0.033F);

			float weapSide = GetWeaponSide();
			bool leftHanded = weapSide < 0.0F;

			// sprint animation
			float sprint = sprintSpring.position;
			mat = CreateRotateMatrix(Vector3(1.0F, 0.0F, 0.0F), sprint * -0.3F) * mat;
			mat = CreateRotateMatrix(Vector3(0.0F, 1.0F, 0.0F), sprint * -0.1F) * mat;
			mat = CreateRotateMatrix(Vector3(0.0F, 0.0F, 1.0F), sprint * -0.2F) * mat;
			mat = CreateTranslateMatrix(Vector3(0.1F, -0.2F, 0.05F) * sprint) * mat;

			// raise animation
			float putdown = raiseSpring.position;
			mat = CreateRotateMatrix(Vector3(1.0F, 0.0F, 0.0F), putdown * -0.5F) * mat;
			mat = CreateRotateMatrix(Vector3(0.0F, 1.0F, 0.0F), putdown * -0.2F) * mat;
			mat = CreateTranslateMatrix(Vector3(0.3F, -0.5F, 0.1F) * putdown) * mat;

			if (readyState > 0.9999F) {
				float bring = Min((readyState - 1.0F) * 2.0F, 1.0F);
				bring = 1.0F - bring;
				bring = 1.0F - bring * bring;

				float pin = 0.0F;
				float side = 0.0F;
				if (cookTime > 0.0001F) {
					pin = Min(cookTime * 8.0F, 2.0F);
					if (pin > 1.0F) {
						side += pin - 1.0F;
						bring -= (pin - 1.0F) * 2.0F;
					}
				}

				// weapon offset, manual adjustment and sway
				Vector3 trans = GetViewOffset(Vector3(-0.3F, 0.7F, 0.2F));
				mat = CreateTranslateMatrix(trans) * mat;

				Matrix4 leftHandMat = mat;
				Matrix4 grenadeMat = mat;
				if (bring < 1.0F) {
					float per = 1.0F - bring;
					leftHandMat = CreateTranslateMatrix(side * 0.5F, per * 0.1F, per * 0.1F) * mat;
					grenadeMat = CreateTranslateMatrix(side * -0.5F, per * -0.2F, 0.0F) * mat;
				}

				// twist the grenade when strafing
				grenadeMat = ApplySwingTwist(grenadeMat);
				
				// apply handedness
				if (leftHanded) {
					Matrix4 invertScale = CreateScaleMatrix(Vector3(-1.0F, 1.0F, 1.0F));
					leftHandMat = invertScale * leftHandMat;
					grenadeMat = invertScale * grenadeMat;
				}
				
				// hands offset
				leftHand = leftHandMat * Vector3(10.0F, 0.0F, 8.0F);
				rightHand = grenadeMat * Vector3(-2.0F, -1.0F, 1.0F);

				// move left hand to grenade pin then move it to the throwing position
				Vector3 pinOffset = leftHandMat * Vector3(2.5F, -0.2F, 0.0F);
				Vector3 throwPos = leftHandMat * Vector3(-0.3F, 0.0F, 4.0F);

				if (pin < 1.0F)
					leftHand = Mix(leftHand, pinOffset, pin);
				else
					leftHand = Mix(pinOffset, throwPos, pin - 1.0F);

				ModelRenderParam param;
				param.depthHack = true;
				param.matrix = eyeMatrix * grenadeMat;
				renderer.AddModel(model, param);
			} else { // throwing
				float per = Min(readyState * 3.0F, 1.0F);

				// left hand shouldn't be visible
				leftHand = Mix(leftHand, Vector3(0.5F * weapSide, 0.5F, 0.6F), per);

				float p2 = per - 0.6F;
				p2 = 0.9F - p2 * p2 * 2.5F;
				rightHand = Vector3(-0.2F * weapSide, p2, -0.9F + per * 1.8F);
			}
		}
	}

	IGrenadeSkin@ CreateViewGrenadeSkin(Renderer@ r, AudioDevice@ dev) {
		return ViewGrenadeSkin(r, dev);
	}
}
