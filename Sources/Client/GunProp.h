#pragma once

#include "ILocalEntity.h"
#include "IModel.h"
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class IRenderer;
		class Client;
		class IAudioChunk;

		class GunProp : public ILocalEntity {
			Client& client;
			IRenderer& renderer;
			Handle<IModel> model;
			Handle<IAudioChunk> bounceSound;
			Vector3 color;
			float scale;
			AABB3 box;
			Matrix4 matrix;
			Vector3 velocity;
			Vector2 rot; // angular speed around local X / Y, in rad/s
			float time; // remaining lifetime, in seconds
			bool resting;
			float groundZ; // z of the surface the item landed on

		public:
			GunProp(Client& client, Handle<IModel> model, Handle<IAudioChunk> bounceSound,
			            const Vector3& color, float scale, const Matrix4& initial,
			            const Vector3& velocity, float lifeTime);
			~GunProp();
			bool Update(float dt) override;
			void Render3D() override;
		};
	} // namespace client
} // namespace spades