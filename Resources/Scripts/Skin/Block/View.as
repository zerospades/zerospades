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
	class ViewBlockSkin : BasicViewTool, IBlockSkin {
		private Vector3 blockColor;
		private float readyState;
		private float lastReadyState = 0.0F;
		private bool hasLastReadyState = false;

		Vector3 BlockColor { set { blockColor = value; } }
		float ReadyState { set { readyState = value; } }

		private Model@ model;

		// Kicked when a block is placed, gives a small rebound on top of the
		// keyframed animation (like the recoil springs of the weapons).
		private ViewWeaponSpring placeSpring = ViewWeaponSpring(150, 12);

		ViewBlockSkin(Renderer@ r, AudioDevice@ dev) {
			super(r, dev);

			@model = renderer.RegisterModel("Models/Weapons/Block/Block.kv6");
		}

		void Update(float dt) {
			// update base script
			BasicViewTool::Update(dt);

			placeSpring.Update(dt);

			// The first update runs before the game has set readyState, so there is
			// nothing to compare against yet. A sudden drop of readyState afterwards
			// means a block was placed.
			if (hasLastReadyState and readyState < lastReadyState - 0.3F)
				placeSpring.velocity += 15.0;
			lastReadyState = readyState;
			hasLastReadyState = true;
		}

		void AddToScene() {
			Matrix4 mat = CreateScaleMatrix(0.033F);

			float weapSide = GetWeaponSide();
			bool leftHanded = weapSide < 0.0F;

			// sprint animation
			float sprint = sprintSpring.position;
			mat = CreateRotateMatrix(Vector3(0.0F, 0.0F, 1.0F), sprint * -0.3F) * mat;
			mat = CreateTranslateMatrix(Vector3(0.1F, -0.4F, -0.05F) * sprint) * mat;

			// raise animation
			mat = CreateTranslateMatrix(Vector3(-0.1F, -0.3F, 0.2F) * raiseSpring.position) * mat;

			// placement animation
			if (readyState < 0.99F) {
				float per = 1.0F - readyState;
				mat = CreateRotateMatrix(Vector3(1.0F, 0.0F, 0.0F), per * 0.8F) * mat;
				mat = CreateTranslateMatrix(Vector3(0.0F, 0.1F, 0.2F) * per) * mat;
			}

			// placement rebound
			float place = placeSpring.position * 5;
			mat = CreateRotateMatrix(Vector3(1.0F, 0.0F, 0.0F), place * 0.25F) * mat;
			mat = CreateTranslateMatrix(Vector3(0.0F, 0.05F, 0.1F) * place) * mat;

			// weapon offset, manual adjustment and sway
			Vector3 trans = GetViewOffset(Vector3(-0.3F, 0.7F, 0.3F));
			mat = CreateTranslateMatrix(trans) * mat;

			// twist the tool when strafing
			mat = ApplySwingTwist(mat);

			// apply handedness
			if (leftHanded)
				mat = CreateScaleMatrix(Vector3(-1.0F, 1.0F, 1.0F)) * mat;

			// hands offset
			leftHand = mat * Vector3(5.0F, -1.0F, 4.0F);
			rightHand = mat * Vector3(-5.5F, 3.0F, -5.0F);

			ModelRenderParam param;
			param.depthHack = true;
			param.customColor = blockColor;
			param.matrix = eyeMatrix * mat;
			renderer.AddModel(model, param);
		}
	}

	IBlockSkin@ CreateViewBlockSkin(Renderer@ r, AudioDevice@ dev) {
		return ViewBlockSkin(r, dev);
	}
}
