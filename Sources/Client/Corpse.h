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

#pragma once

#include "IRenderer.h"
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class IRenderer;
		class GameMap;
		class Player;
		class IModel;

		class Corpse {
			enum NodeType {
				// torso in CW seen from front
				Torso1,
				Torso2,
				Torso3,
				Torso4,

				Arm1, // Torso1
				Arm2, // Torso2
				Leg1, // Torso3
				Leg2, // Torso4

				Head,

				NodeCount
			};

			struct Node {
				Vector3 pos, vel;
				Vector3 lastPos;
				Vector3 lastForce;
			};

			struct Edge {
				NodeType node1, node2;
				Vector3 lastVelDiff;
				Vector3 velDiff;
				Edge() { node1 = node2 = NodeCount; }
			};

			Handle<IRenderer> renderer; // may be a skin's: the corpse keeps it alive
			GameMap& map;
			int playerId;
			Vector3 color;
			std::string weaponName;
			bool isLocalPlayer;
			bool isZombiePlayer;
			bool isChristmasOn;

			Node nodes[NodeCount];
			Edge edges[8];

			void SetNode(NodeType n, Vector3);
			void SetNode(NodeType n, Vector4);

			void Spring(NodeType n1, NodeType n2, float distance, float dt);
			void Spring(NodeType n1a, NodeType n1b, NodeType n2, float distance, float dt);
			void AngleSpring(NodeType base, NodeType a, NodeType b,
				float minDot, float maxDot, float dt);
			void AngleSpring(NodeType a, NodeType b, Vector3 dir,
				float minDot, float maxDot, float dt);
			void AngularMomentum(int eId, NodeType a, NodeType b);
			void ApplyConstraint(float dt);
			void LineCollision(NodeType a, NodeType b, float dt);

		public:
			/**
			 * Construct a "corpse" client object.
			 *
			 * @param renderer The renderer: its player's, which the corpse keeps.
			 * @param map The game map, used for physics. Must outlive `Corpse`.
			 * @param p The player to create a corpse from. Can be destroyed
			 *			after `Corpse` is constructed.
			 */
			Corpse(IRenderer& renderer, GameMap& map, Player& p, bool isChristmasOn);
			~Corpse();

			void Update(float dt);

			int GetPlayerId() { return playerId; }

			void AddToScene();

			Vector3 GetCenter();
			bool IsVisibleFrom(Vector3 eye);

			void AddHeadImpulse(Vector3);
			void AddImpulse(Vector3);
		};
	} // namespace client
} // namespace spades