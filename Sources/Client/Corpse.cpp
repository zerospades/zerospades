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

#include "Corpse.h"
#include "GameMap.h"
#include "IModel.h"
#include "IRenderer.h"
#include "Player.h"
#include "Weapon.h"
#include "World.h"
#include <Core/Debug.h>
#include <Core/Settings.h>

using namespace std;

DEFINE_SPADES_SETTING(cg_corpseLineCollision, "0");
DEFINE_SPADES_SETTING(cg_corpseDisableGravity, "0");

SPADES_SETTING(cg_classicPlayerModels);

namespace spades {
	namespace client {
		Corpse::Corpse(IRenderer& renderer, GameMap& map, Player& p, bool isChristmasOn)
			: renderer{renderer}, map{map}, isChristmasOn{isChristmasOn} {
			SPADES_MARK_FUNCTION();

			playerId = p.GetId();
			color = ConvertColorRGB(p.GetColor());
			weaponName = p.GetWeapon().GetName();
			isLocalPlayer = p.IsLocalPlayer();
			isZombiePlayer = p.IsZombie();

			Vector3 o = p.GetFront();

			float yaw = atan2f(o.y, o.x) + kHalfPi;
			float pitch = -atan2f(o.z, o.GetLength2D());

			float armPitch = pitch;
			if (armPitch < 0.0F)
				armPitch = std::max(armPitch, -kHalfPi) * 0.9F;

			// lower axis
			Matrix4 lower = Matrix4::Translate(p.GetOrigin())
				* Matrix4::Rotate(MakeVector3(0, 0, 1), yaw);

			Matrix4 torso;

			if (p.GetInput().crouch) {
				lower = lower * Matrix4::Translate(0, 0, -0.4F);
				torso = lower * Matrix4::Translate(0, 0, -0.3F);

				SetNode(Torso1, torso * MakeVector3(0.4F, -0.15F, 0.1F));
				SetNode(Torso2, torso * MakeVector3(-0.4F, -0.15F, 0.1F));
				SetNode(Torso3, torso * MakeVector3(-0.4F, 0.8F, 0.7F));
				SetNode(Torso4, torso * MakeVector3(0.4F, 0.8F, 0.7F));
			} else {
				torso = lower * Matrix4::Translate(0, 0, -1.1F);

				SetNode(Torso1, torso * MakeVector3(0.4F, 0, 0.1F));
				SetNode(Torso2, torso * MakeVector3(-0.4F, 0, 0.1F));
				SetNode(Torso3, torso * MakeVector3(-0.4F, 0, 1));
				SetNode(Torso4, torso * MakeVector3(0.4F, 0, 1));
			}

			Matrix4 const head = torso
				* Matrix4::Rotate(MakeVector3(1, 0, 0), pitch);
			Matrix4 const arms = torso
				* Matrix4::Rotate(MakeVector3(1, 0, 0), armPitch);

			SetNode(Leg1, lower * MakeVector3(-0.4F, 0, 1));
			SetNode(Leg2, lower * MakeVector3(0.4F, 0, 1));
			SetNode(Arm1, arms * MakeVector3(0.4F, -0.8F, 0.1F));
			SetNode(Arm2, arms * MakeVector3(0.1F, -0.8F, 0.1F));
			SetNode(Head, head * MakeVector3(0, 0, -0.9F));
		}

		void Corpse::SetNode(NodeType n, spades::Vector3 v) {
			auto velNoise = [&] { return (SampleRandomFloat() - SampleRandomFloat()) * 2.0F; };

			SPAssert(n >= 0);
			SPAssert(n < NodeCount);

			nodes[n].pos = v;
			nodes[n].vel = MakeVector3(velNoise(), velNoise(), 0.0F);
			nodes[n].lastPos = v;
			nodes[n].lastForce = MakeVector3(0, 0, 0);
		}
		void Corpse::SetNode(NodeType n, spades::Vector4 v) { SetNode(n, v.GetXYZ()); }

		Corpse::~Corpse() {}

		void Corpse::Spring(NodeType n1, NodeType n2, float distance, float dt) {
			SPADES_MARK_FUNCTION_DEBUG();
			SPAssert(n1 >= 0);
			SPAssert(n1 < NodeCount);
			SPAssert(n2 >= 0);
			SPAssert(n2 < NodeCount);
			Node& a = nodes[n1];
			Node& b = nodes[n2];
			Vector3 diff = b.pos - a.pos;
			float dist = diff.GetLength();
			Vector3 force = diff.Normalize() * (distance - dist);
			force *= dt * 50.0F;

			b.vel += force;
			a.vel -= force;

			b.pos += force / (dt * 50.0F) * 0.5F;
			a.pos -= force / (dt * 50.0F) * 0.5F;

			Vector3 velMid = (a.vel + b.vel) * 0.5F;
			float dump = 1.0F - powf(0.1F, dt);
			a.vel += (velMid - a.vel) * dump;
			b.vel += (velMid - b.vel) * dump;
		}
		void Corpse::Spring(NodeType n1a, NodeType n1b, NodeType n2, float distance, float dt) {
			SPADES_MARK_FUNCTION_DEBUG();
			SPAssert(n1a >= 0);
			SPAssert(n1a < NodeCount);
			SPAssert(n1b >= 0);
			SPAssert(n1b < NodeCount);
			SPAssert(n2 >= 0);
			SPAssert(n2 < NodeCount);
			Node& x = nodes[n1a];
			Node& y = nodes[n1b];
			Node& b = nodes[n2];
			Vector3 diff = b.pos - (x.pos + y.pos) * 0.5F;
			float dist = diff.GetLength();
			Vector3 force = diff.Normalize() * (distance - dist);
			force *= dt * 50.0F;

			b.vel += force;
			force *= 0.5F;
			x.vel -= force;
			y.vel -= force;

			Vector3 velMid = (x.vel + y.vel) * 0.25F + b.vel * 0.5F;
			float dump = 1.0F - powf(0.05F, dt);
			x.vel += (velMid - x.vel) * dump;
			y.vel += (velMid - y.vel) * dump;
			b.vel += (velMid - b.vel) * dump;
		}

		static float MyACos(float v) {
			SPAssert(!isnan(v));
			if (v >= 1.0F)
				return 0.0F;
			if (v <= -1.0F)
				return kPi;
			float vv = acosf(v);
			if (isnan(vv))
				vv = acosf(v * 0.99F);
			SPAssert(!isnan(vv));
			return vv;
		}

		void Corpse::AngleSpring(NodeType base, NodeType n1id, NodeType n2id, float minDot,
								 float maxDot, float dt) {
			Node& nBase = nodes[base];
			Node& n1 = nodes[n1id];
			Node& n2 = nodes[n2id];
			Vector3 d1 = n1.pos - nBase.pos;
			Vector3 d2 = n2.pos - nBase.pos;
			float ln1 = d1.GetLength();
			float ln2 = d2.GetLength();

			float dot = Vector3::Dot(d1, d2) / (ln1 * ln2 + 0.0000001F);
			if (dot <= maxDot && dot >= minDot)
				return;

			Vector3 diff = n2.pos - n1.pos;
			float strength = 0.0F;

			Vector3 a1 = Vector3::Cross(d1, diff);
			a1 = Vector3::Cross(d1, a1).Normalize();

			Vector3 a2 = Vector3::Cross(d2, diff);
			a2 = Vector3::Cross(d2, a2).Normalize();
			a2 = -a2;

			if (dot > maxDot)
				strength = MyACos(dot) - MyACos(maxDot);
			else if (dot < minDot)
				strength = MyACos(dot) - MyACos(minDot);

			SPAssert(!isnan(strength));

			strength *= 20.0F;
			strength *= dt;

			a1 *= strength;
			a2 *= strength;

			a2 *= 0.0F;

			n2.vel += a1;
			n1.vel += a2;
			nBase.vel -= a1 + a2;
		}
		void Corpse::AngleSpring(NodeType n1id, NodeType n2id, Vector3 dir, float minDot,
								 float maxDot, float dt) {
			Node& n1 = nodes[n1id];
			Node& n2 = nodes[n2id];
			Vector3 diff = n2.pos - n1.pos;
			float ln1 = diff.GetLength();
			float dot = Vector3::Dot(diff, dir) / (ln1 + 0.000000001F);

			if (dot <= maxDot && dot >= minDot)
				return;

			float strength = 0.0F;

			Vector3 a1 = Vector3::Cross(dir, diff);
			a1 = Vector3::Cross(diff, a1).Normalize();
			Vector3 a2 = -a1;

			if (dot > maxDot)
				strength = MyACos(dot) - MyACos(maxDot);
			else if (dot < minDot)
				strength = MyACos(dot) - MyACos(minDot);

			SPAssert(!isnan(strength));

			strength *= 100.0F;
			strength *= dt;

			a1 *= strength;
			a2 *= strength;

			n2.vel += a1;
			n1.vel += a2;
		}

		static float fractf(float v) { return v - floorf(v); }

		static void CheckEscape(GameMap& map, IntVector3 hitBlock, Vector3 a, Vector3 b,
								IntVector3 dir, float& bestDist, IntVector3& bestDir) {
			hitBlock += dir;
			IntVector3 aa = a.Floor() + dir;
			IntVector3 bb = b.Floor() + dir;

			if (map.IsSolidWrapped(hitBlock.x, hitBlock.y, hitBlock.z))
				return;
			if (map.IsSolidWrapped(aa.x, aa.y, aa.z))
				return;
			if (map.IsSolidWrapped(bb.x, bb.y, bb.z))
				return;

			float dist;
			if (dir.x == 1) {
				dist = 1.0F - fractf(a.x);
				dist += 1.0F - fractf(b.x);
			} else if (dir.x == -1) {
				dist = fractf(a.x);
				dist += fractf(b.x);
			} else if (dir.y == 1) {
				dist = 1.0F - fractf(a.y);
				dist += 1.0F - fractf(b.y);
			} else if (dir.y == -1) {
				dist = fractf(a.y);
				dist += fractf(b.y);
			} else if (dir.z == 1) {
				dist = 1.0F - fractf(a.z);
				dist += 1.0F - fractf(b.z);
			} else if (dir.z == -1) {
				dist = fractf(a.z);
				dist += fractf(b.z);
			} else {
				SPAssert(false);
				return;
			}

			if (dist < bestDist) {
				bestDist = dist;
				bestDir = dir;
			}
		}

		void Corpse::LineCollision(NodeType a, NodeType b, float dt) {
			Node& n1 = nodes[a];
			Node& n2 = nodes[b];

			IntVector3 hitBlock;

			if (map.CastRay(n1.lastPos, n2.lastPos, 16.0F, hitBlock)) {
				GameMap::RayCastResult res1 = map.CastRay2(n1.lastPos, n2.lastPos - n1.lastPos, 8);
				GameMap::RayCastResult res2 = map.CastRay2(n2.lastPos, n1.lastPos - n2.lastPos, 8);

				if (!res1.hit || !res2.hit || res1.startSolid || res2.startSolid)
					return;

				// really hit?
				if (Vector3::Dot(res1.hitPos - n1.lastPos, n2.lastPos - n1.lastPos) >
					(n2.pos - n1.pos).GetSquaredLength())
					return;
				if (Vector3::Dot(res1.hitPos - n1.lastPos, n2.lastPos - n1.lastPos) < 0.0F)
					return;
				if (Vector3::Dot(res2.hitPos - n2.lastPos, n1.lastPos - n2.lastPos) >
					(n2.pos - n1.pos).GetSquaredLength())
					return;
				if (Vector3::Dot(res2.hitPos - n2.lastPos, n1.lastPos - n2.lastPos) < 0.0F)
					return;

				float inlen = (res1.hitPos - res2.hitPos).GetLength();

				IntVector3 ivec = {0, 0, 0};
				ivec.x += res1.normal.x;
				ivec.y += res1.normal.y;
				ivec.z += res1.normal.z;
				ivec.x += res2.normal.x;
				ivec.y += res2.normal.y;
				ivec.z += res2.normal.z;

				Vector3 dir = {0.0F, 0.0F, 0.0F};
				if (ivec.x == 0 && ivec.y == 0 && ivec.z == 0) {
					// hanging. which direction to escape?
					float bestDist = 1000.0F;
					IntVector3 bestDir;
					CheckEscape(map, hitBlock, n1.pos, n2.pos,
						MakeIntVector3(1, 0, 0), bestDist, bestDir);
					CheckEscape(map, hitBlock, n1.pos, n2.pos,
						MakeIntVector3(-1, 0, 0), bestDist, bestDir);
					CheckEscape(map, hitBlock, n1.pos, n2.pos,
						MakeIntVector3(0, 1, 0), bestDist, bestDir);
					CheckEscape(map, hitBlock, n1.pos, n2.pos,
						MakeIntVector3(0, -1, 0), bestDist, bestDir);
					CheckEscape(map, hitBlock, n1.pos, n2.pos,
						MakeIntVector3(0, 0, 1), bestDist, bestDir);
					CheckEscape(map, hitBlock, n1.pos, n2.pos,
						MakeIntVector3(0, 0, -1), bestDist, bestDir);

					if (bestDist > 10.0F)
						return; // failed to find appropriate direction.

					ivec = bestDir;
					inlen = bestDist + 0.1F;
				}

				dir = MakeVector3(ivec);

				Vector3 normDir = dir; // |D|

				n1.vel -= normDir * std::min(Vector3::Dot(normDir, n1.vel), 0.0F);
				n2.vel -= normDir * std::min(Vector3::Dot(normDir, n2.vel), 0.0F);

				dir *= dt * inlen * 5.0F;

				n1.vel += dir;
				n2.vel += dir;

				// friction
				n1.vel -= (n1.vel - normDir * Vector3::Dot(normDir, n1.vel)) * 0.2F;
				n2.vel -= (n2.vel - normDir * Vector3::Dot(normDir, n2.vel)) * 0.2F;
			}
		}

		void Corpse::AngularMomentum(int eId, NodeType a, NodeType b) {
			Edge& e = edges[eId];
			e.velDiff = nodes[b].vel - nodes[a].vel;
			if (e.node1 != a || e.node2 != b) {
				e.lastVelDiff = e.velDiff;
				e.node1 = a;
				e.node2 = b;
				return;
			}

			Vector3 force = e.lastVelDiff - e.velDiff;
			force *= 0.5F;
			nodes[b].vel += force;
			nodes[a].vel -= force;

			e.lastVelDiff = e.velDiff;
		}

		void Corpse::ApplyConstraint(float dt) {
			SPADES_MARK_FUNCTION();

			AngularMomentum(0, Torso1, Torso2);
			AngularMomentum(1, Torso2, Torso3);
			AngularMomentum(2, Torso3, Torso4);
			AngularMomentum(3, Torso4, Torso1);
			AngularMomentum(4, Torso1, Arm1);
			AngularMomentum(5, Torso2, Arm2);
			AngularMomentum(6, Torso3, Leg1);
			AngularMomentum(7, Torso4, Leg2);

			Spring(Torso1, Torso2, 0.8F, dt);
			Spring(Torso3, Torso4, 0.8F, dt);

			Spring(Torso1, Torso4, 0.9F, dt);
			Spring(Torso2, Torso3, 0.9F, dt);

			Spring(Torso1, Torso3, 1.2F, dt);
			Spring(Torso2, Torso4, 1.2F, dt);

			Spring(Arm1, Torso1, 1.0F, dt);
			Spring(Arm2, Torso2, 1.0F, dt);
			Spring(Leg1, Torso3, 1.15F, dt);
			Spring(Leg2, Torso4, 1.15F, dt);

			Spring(Torso1, Torso2, Head, 0.6F, dt);

			AngleSpring(Torso1, Arm1, Torso3, -1.0F, 0.6F, dt);
			AngleSpring(Torso2, Arm2, Torso4, -1.0F, 0.6F, dt);

			AngleSpring(Torso3, Leg1, Torso2, -1.0F, -0.2F, dt);
			AngleSpring(Torso4, Leg2, Torso1, -1.0F, -0.2F, dt);

			AngleSpring(Torso1, Torso2, Head, 0.5F, 1.0F, dt);
			AngleSpring(Torso2, Torso1, Head, 0.5F, 1.0F, dt);

			if (cg_corpseLineCollision) {
				LineCollision(Torso1, Torso2, dt);
				LineCollision(Torso2, Torso3, dt);
				LineCollision(Torso3, Torso4, dt);
				LineCollision(Torso4, Torso1, dt);
				LineCollision(Torso1, Torso3, dt);
				LineCollision(Torso2, Torso4, dt);
				LineCollision(Torso1, Arm1, dt);
				LineCollision(Torso2, Arm2, dt);
				LineCollision(Torso3, Leg1, dt);
				LineCollision(Torso4, Leg2, dt);
			}
		}

		void Corpse::Update(float dt) {
			SPADES_MARK_FUNCTION();

			bool disableGravity = !isLocalPlayer && cg_corpseDisableGravity;
			float gravityScale = disableGravity ? 0.0F : 1.0F;

			float damp = 1.0F;
			float damp2 = 1.0F;

			if (dt > 0.0F) {
				damp = powf(0.9F, dt);
				damp2 = powf(0.371F, dt);
			}

			for (int i = 0; i < NodeCount; i++) {
				Node& node = nodes[i];
				Vector3 oldPos = node.lastPos;
				node.pos += node.vel * dt;

				SPAssert(!node.pos.IsNaN());

				if (node.pos.z >= 63.0F) {
					node.vel.z -= dt * 6.0F; // buoyancy
					node.vel *= damp;
				} else {
					node.vel.z += dt * 32.0F * gravityScale; // gravity
					node.vel.z *= damp2;
				}

				if (!map.ClipBox(oldPos.x, oldPos.y, oldPos.z)) {
					if (map.ClipBox(node.pos.x, oldPos.y, oldPos.z)) {
						node.vel.x = -node.vel.x;
						node.vel.x *= 0.2F;

						if (fabsf(node.vel.x) < 0.3F)
							node.vel.x = 0.0F;

						node.pos.x = oldPos.x;
						node.vel.y *= 0.5F;
						node.vel.z *= 0.5F;
					}

					if (map.ClipBox(node.pos.x, node.pos.y, oldPos.z)) {
						node.vel.y = -node.vel.y;
						node.vel.y *= 0.2F;

						if (fabsf(node.vel.y) < 0.3F)
							node.vel.y = 0.0F;

						node.pos.y = oldPos.y;
						node.vel.x *= 0.5F;
						node.vel.z *= 0.5F;
					}

					if (map.ClipBox(node.pos.x, node.pos.y, node.pos.z)) {
						node.vel.z = -node.vel.z;
						node.vel.z *= 0.2F;

						if (fabsf(node.vel.z) < 0.3F)
							node.vel.z = 0.0F;

						node.pos.z = oldPos.z;
						node.vel.x *= 0.5F;
						node.vel.y *= 0.5F;
					}
				}

				node.lastPos = node.pos;
				node.lastForce = node.vel;
			}
			ApplyConstraint(dt);

			for (int i = 0; i < NodeCount; i++)
				nodes[i].lastForce = nodes[i].vel - nodes[i].lastForce;
		}

		void Corpse::AddToScene() {
			Handle<IModel> model;
			ModelRenderParam param;
			param.customColor = color;

			std::string modelPath = "Models/Player/";
			if (isZombiePlayer)
				modelPath += "Zombie/";
			else if (!cg_classicPlayerModels)
				modelPath += weaponName + "/";

			const Matrix4 scaler = Matrix4::Scale(0.1F);
			Matrix4 torso;

			const Vector3 tX1 = nodes[Torso1].pos - nodes[Torso2].pos;
			const Vector3 tX2 = nodes[Torso4].pos - nodes[Torso3].pos;
			const Vector3 tY1 = nodes[Torso1].pos + nodes[Torso2].pos;
			const Vector3 tY2 = nodes[Torso4].pos + nodes[Torso3].pos;
			const Vector3 center = tY1 * 0.5F;

			// Torso
			{
				model = renderer->RegisterModel((modelPath + "Torso.kv6").c_str());

				Vector3 tX, tY, tZ;
				tX = ((tX1 + tX2) * 0.5F).Normalize();
				tY = ((tY2 - tY1) * 0.5F).Normalize();
				tZ = Vector3::Cross(tX, tY).Normalize();
				tY = Vector3::Cross(tX, tZ).Normalize();
				torso = Matrix4::FromAxis(tX, -tZ, -tY, center);

				param.matrix = torso * scaler * Matrix4::Scale(-1, -1, 1); // rotate
				renderer->RenderModel(*model, param);
			}

			// Head
			{
				model = isChristmasOn
					? renderer->RegisterModel("Models/Player/XmasHat.kv6")
					: renderer->RegisterModel((modelPath + "Head.kv6").c_str());

				const Vector3 headBase = (torso * MakeVector3(0.0F, 0.0F, 0.0F)).GetXYZ();

				Vector3 aX, aY, aZ;
				aZ = ((nodes[Head].pos - center) - torso.GetAxis(2)).Normalize();
				aY = Vector3::Cross(-tX1, aZ).Normalize();
				aX = Vector3::Cross(aY, aZ).Normalize();
				param.matrix = Matrix4::FromAxis(-aX, aY, -aZ, headBase) * scaler;
				renderer->RenderModel(*model, param);
			}

			// Arms
			{
				const Vector3 arm1Base = (torso * MakeVector3(0.4F, 0.0F, 0.1F)).GetXYZ();
				const Vector3 arm2Base = (torso * MakeVector3(-0.4F, 0.0F, 0.1F)).GetXYZ();

				const Matrix4 armsScale = scaler
					* Matrix4::Scale(0.75F, 0.75F, 1.0F)
					* Matrix4::Scale(-1, -1, 1);

				model = renderer->RegisterModel((modelPath + "ArmRight.kv6").c_str());
				Vector3 aX, aY, aZ;
				aZ = (nodes[Arm1].pos - nodes[Torso1].pos).Normalize();
				aY = Vector3::Cross(tX1, aZ).Normalize();
				aX = Vector3::Cross(aY, aZ).Normalize();
				param.matrix = Matrix4::FromAxis(aY, -aX, aZ, arm1Base) * armsScale;
				renderer->RenderModel(*model, param);

				model = renderer->RegisterModel((modelPath + "Arm.kv6").c_str());
				aZ = (nodes[Arm2].pos - nodes[Torso2].pos).Normalize();
				aY = Vector3::Cross(tX1, aZ).Normalize();
				aX = Vector3::Cross(aY, aZ).Normalize();
				param.matrix = Matrix4::FromAxis(aY, -aX, aZ, arm2Base) * armsScale;
				renderer->RenderModel(*model, param);
			}

			// Legs
			{
				const Vector3 leg1Base = (torso * MakeVector3(0.25F, 0.0F, 0.9F)).GetXYZ();
				const Vector3 leg2Base = (torso * MakeVector3(-0.25F, 0.0F, 0.9F)).GetXYZ();

				model = renderer->RegisterModel((modelPath + "LegRight.kv6").c_str());
				Vector3 aX, aY, aZ;
				aZ = (nodes[Leg1].pos - nodes[Torso3].pos).Normalize();
				aY = Vector3::Cross(tX1, aZ).Normalize();
				aX = Vector3::Cross(aY, aZ).Normalize();
				param.matrix = Matrix4::FromAxis(aX, aY, aZ, leg1Base) * scaler;
				renderer->RenderModel(*model, param);

				model = renderer->RegisterModel((modelPath + "Leg.kv6").c_str());
				aZ = (nodes[Leg2].pos - nodes[Torso4].pos).Normalize();
				aY = Vector3::Cross(tX1, aZ).Normalize();
				aX = Vector3::Cross(aY, aZ).Normalize();
				param.matrix = Matrix4::FromAxis(aX, aY, aZ, leg2Base) * scaler;
				renderer->RenderModel(*model, param);
			}
		}

		Vector3 Corpse::GetCenter() {
			Vector3 v = {0, 0, 0};
			for (int i = 0; i < NodeCount; i++)
				v += nodes[i].pos;
			v *= 1.0F / (float)NodeCount;
			return v;
		}

		bool Corpse::IsVisibleFrom(spades::Vector3 eye) {
			// distance culled?
			if ((GetCenter() - eye).GetSquaredLength2D() > FOG_DISTANCE_SQ)
				return false;

			for (int i = 0; i < NodeCount; i++) {
				IntVector3 outBlk;
				if (map.CastRay(eye, nodes[i].pos, 256.0F, outBlk))
					return true;
			}

			return false;
		}

		void Corpse::AddHeadImpulse(spades::Vector3 v) {
			nodes[Head].vel += v;
		}
		void Corpse::AddImpulse(spades::Vector3 v) {
			for (int i = 0; i < NodeCount; i++)
				nodes[i].vel += v;
		}
	} // namespace client
} // namespace spades