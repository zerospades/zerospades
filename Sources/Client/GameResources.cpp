/*
 Copyright (c) 2026 ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include <thread>

#include "GameResources.h"
#include "IAudioDevice.h"
#include "IRenderer.h"
#include <Core/AudioStream.h>
#include <Core/Bitmap.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/GameAsset.h>
#include <Core/IAudioStream.h>
#include <Core/TMPUtils.h>
#include <Core/VoxelModel.h>
#include <Core/VoxelModelLoader.h>

namespace spades {
	namespace client {
		namespace {
			// The one way each kind of game asset is loaded.
			Handle<IImage> LoadImage(IRenderer& renderer, const std::string& path) {
				return renderer.CreateImage(*Bitmap::Load(path));
			}
			Handle<IModel> LoadModel(IRenderer& renderer, const std::string& path) {
				return renderer.CreateModel(*VoxelModelLoader::Load(path.c_str()));
			}
			Handle<IAudioChunk> LoadSound(IAudioDevice& audio, const std::string& path) {
				return audio.CreateSound(path.c_str());
			}

			// Skin::Scope only reaches the thread that opens it.
			std::thread::id g_loadThread = std::this_thread::get_id();

			template <class T, class Load>
			Handle<T> Register(std::map<std::string, Handle<T>>& cache, const std::string& path,
			                   const Skin* skin, Load load) {
				// Checked in every build: a load elsewhere would silently miss the skin.
				if (std::this_thread::get_id() != g_loadThread)
					SPRaise("Game assets load on the main thread only: '%s'", path.c_str());
				auto it = cache.find(path);
				if (it != cache.end())
					return it->second;
				Handle<T> object;
				{
					std::unique_ptr<Skin::Scope> scope;
					if (skin)
						scope = stmp::make_unique<Skin::Scope>(*skin);
					object = load(path);
				}
				cache[path] = object;
				return object;
			}

			/** Draws with a base renderer, and loads through a GameResources. */
			class GameRenderer : public IRenderer {
				Handle<IRenderer> base;
				Handle<GameResources> resources;
				Handle<Skin> skin;

			protected:
				~GameRenderer() {}

			public:
				GameRenderer(Handle<IRenderer> base, Handle<GameResources> resources,
				             Handle<Skin> skin)
				    : base(std::move(base)), resources(std::move(resources)), skin(std::move(skin)) {}

				void Init() override { base->Init(); }
				void Shutdown() override { base->Shutdown(); }

				Handle<IImage> RegisterImage(const char* filename) override {
					return resources->RegisterImage(filename, skin.GetPointerOrNull());
				}
				Handle<IModel> RegisterModel(const char* filename) override {
					return resources->RegisterModel(filename, skin.GetPointerOrNull());
				}
				bool IsBuiltInImage(const char* name) override { return base->IsBuiltInImage(name); }
				// The game's files are its own: the software's are the base renderer's.
				std::vector<std::string> GetRegisteredFiles() override { return {}; }

				Handle<IImage> CreateImage(Bitmap& bmp) override { return base->CreateImage(bmp); }
				Handle<IModel> CreateModel(VoxelModel& m) override { return base->CreateModel(m); }

				void SetGameMap(stmp::optional<GameMap&> map) override { base->SetGameMap(map); }
				void SetFogDistance(float d) override { base->SetFogDistance(d); }
				void SetFogColor(Vector3 c) override { base->SetFogColor(c); }

				void StartScene(const SceneDefinition& def) override { base->StartScene(def); }
				void AddLight(const DynamicLightParam& light) override { base->AddLight(light); }
				void RenderModel(IModel& model, const ModelRenderParam& p) override {
					base->RenderModel(model, p);
				}
				void AddDebugLine(Vector3 a, Vector3 b, Vector4 color) override {
					base->AddDebugLine(a, b, color);
				}
				void AddSprite(IImage& img, Vector3 center, float radius, float rotation) override {
					base->AddSprite(img, center, radius, rotation);
				}
				void AddLongSprite(IImage& img, Vector3 p1, Vector3 p2, float radius) override {
					base->AddLongSprite(img, p1, p2, radius);
				}
				void EndScene() override { base->EndScene(); }

				void MultiplyScreenColor(Vector3 c) override { base->MultiplyScreenColor(c); }
				void SetColor(Vector4 c) override { base->SetColor(c); }
				void SetColorAlphaPremultiplied(Vector4 c) override {
					base->SetColorAlphaPremultiplied(c);
				}

				void DrawImage(stmp::optional<IImage&> img, const Vector2& outTopLeft) override {
					base->DrawImage(img, outTopLeft);
				}
				void DrawImage(stmp::optional<IImage&> img, const AABB2& outRect) override {
					base->DrawImage(img, outRect);
				}
				void DrawImage(stmp::optional<IImage&> img, const Vector2& outTopLeft,
				               const AABB2& inRect) override {
					base->DrawImage(img, outTopLeft, inRect);
				}
				void DrawImage(stmp::optional<IImage&> img, const AABB2& outRect,
				               const AABB2& inRect) override {
					base->DrawImage(img, outRect, inRect);
				}
				void DrawImage(stmp::optional<IImage&> img, const Vector2& outTopLeft,
				               const Vector2& outTopRight, const Vector2& outBottomLeft,
				               const AABB2& inRect) override {
					base->DrawImage(img, outTopLeft, outTopRight, outBottomLeft, inRect);
				}

				void DrawFlatGameMap(const AABB2& outRect, const AABB2& inRect) override {
					base->DrawFlatGameMap(outRect, inRect);
				}
				void DrawFlatGameMap(const Vector2& outTopLeft, const Vector2& outTopRight,
				                     const Vector2& outBottomLeft, const AABB2& inRect) override {
					base->DrawFlatGameMap(outTopLeft, outTopRight, outBottomLeft, inRect);
				}

				void BeginClippingCircle(const Vector2& center, float radius) override {
					base->BeginClippingCircle(center, radius);
				}
				void EndClippingCircle() override { base->EndClippingCircle(); }
				void BeginClippingRect(const AABB2& outRect) override {
					base->BeginClippingRect(outRect);
				}
				void EndClippingRect() override { base->EndClippingRect(); }

				bool IsRendererSW() const override { return base->IsRendererSW(); }

				void FrameDone() override { base->FrameDone(); }
				void Flip() override { base->Flip(); }
				Handle<Bitmap> ReadBitmap() override { return base->ReadBitmap(); }

				float ScreenWidth() override { return base->ScreenWidth(); }
				float ScreenHeight() override { return base->ScreenHeight(); }

				void DrawFilledRectFade(float x0, float y0, float x1, float y1, Vector4 color0,
				                        Vector4 color1, bool horizontal) override {
					base->DrawFilledRectFade(x0, y0, x1, y1, color0, color1, horizontal);
				}
				void DrawFilledTriangle(const Vector2& v0, const Vector2& v1,
				                        const Vector2& v2) override {
					base->DrawFilledTriangle(v0, v1, v2);
				}
			};

			/** Plays with a base audio device, and loads through a GameResources. */
			class GameAudioDevice : public IAudioDevice {
				Handle<IAudioDevice> base;
				Handle<GameResources> resources;
				Handle<Skin> skin;

			protected:
				~GameAudioDevice() {}

			public:
				GameAudioDevice(Handle<IAudioDevice> base, Handle<GameResources> resources,
				                Handle<Skin> skin)
				    : base(std::move(base)), resources(std::move(resources)), skin(std::move(skin)) {}

				Handle<IAudioChunk> RegisterSound(const char* name) override {
					return resources->RegisterSound(name, skin.GetPointerOrNull());
				}
				std::vector<std::string> GetRegisteredFiles() override { return {}; }
				Handle<IAudioChunk> CreateSound(const char* name) override {
					return base->CreateSound(name);
				}

				void SetGameMap(GameMap* map) override { base->SetGameMap(map); }

				void Play(IAudioChunk* chunk, const Vector3& origin,
				          const AudioParam& param) override {
					base->Play(chunk, origin, param);
				}
				void PlayLocal(IAudioChunk* chunk, const Vector3& origin,
				               const AudioParam& param) override {
					base->PlayLocal(chunk, origin, param);
				}
				void PlayLocal(IAudioChunk* chunk, const AudioParam& param) override {
					base->PlayLocal(chunk, param);
				}
				void Respatialize(const Vector3& eye, const Vector3& front,
				                  const Vector3& up) override {
					base->Respatialize(eye, front, up);
				}
			};
		} // namespace

		GameResources::GameResources(Handle<IRenderer> renderer, Handle<IAudioDevice> audio)
		    : renderer(std::move(renderer)), audio(std::move(audio)) {}

		GameResources::~GameResources() {}

		GameResources::Cache& GameResources::GetCache(Skin* skin, const std::string& path) {
			if (!skin || !skin->Has(path))
				return shared;
			auto& entry = skinned[skin];
			if (!entry.first)
				entry.first = Handle<Skin>(*skin);
			return entry.second;
		}

		Handle<IImage> GameResources::RegisterImage(const std::string& path, Skin* skin) {
			SPADES_MARK_FUNCTION();
			// Not a file, such as "*white": the renderer makes it, once.
			if (renderer->IsBuiltInImage(path.c_str()))
				return renderer->RegisterImage(path.c_str());
			Cache& cache = GetCache(skin, path);
			const Skin* from = &cache == &shared ? nullptr : skin;
			return Register(cache.images, path, from,
			                [&](const std::string& p) { return LoadImage(*renderer, p); });
		}

		Handle<IModel> GameResources::RegisterModel(const std::string& path, Skin* skin) {
			SPADES_MARK_FUNCTION();
			Cache& cache = GetCache(skin, path);
			const Skin* from = &cache == &shared ? nullptr : skin;
			return Register(cache.models, path, from,
			                [&](const std::string& p) { return LoadModel(*renderer, p); });
		}

		Handle<IAudioChunk> GameResources::RegisterSound(const std::string& path, Skin* skin) {
			SPADES_MARK_FUNCTION();
			Cache& cache = GetCache(skin, path);
			const Skin* from = &cache == &shared ? nullptr : skin;
			return Register(cache.sounds, path, from,
			                [&](const std::string& p) { return LoadSound(*audio, p); });
		}

		Handle<IRenderer> GameResources::CreateRenderer(Handle<Skin> skin) {
			return Handle<GameRenderer>::New(renderer, Handle<GameResources>(*this), std::move(skin))
			  .Cast<IRenderer>();
		}

		Handle<IAudioDevice> GameResources::CreateAudioDevice(Handle<Skin> skin) {
			return Handle<GameAudioDevice>::New(audio, Handle<GameResources>(*this),
			                                    std::move(skin))
			  .Cast<IAudioDevice>();
		}

		void GameResources::Preload(Skin& skin) {
			SPADES_MARK_FUNCTION();

			for (const std::string& path : skin.GetAssets()) {
				try {
					switch (GetGameAssetKind(path)) {
						case GameAssetKind::Image: RegisterImage(path, &skin); break;
						case GameAssetKind::Model: RegisterModel(path, &skin); break;
						case GameAssetKind::Sound: RegisterSound(path, &skin); break;
						default: break;
					}
				} catch (const std::exception& ex) {
					SPRaise("%s: %s", path.c_str(), ex.what());
				}
			}
		}

		void GameResources::CheckFile(const std::string& path) {
			SPADES_MARK_FUNCTION();

			switch (GetGameAssetKind(path)) {
				case GameAssetKind::Image: Bitmap::Load(path); break;
				case GameAssetKind::Model: VoxelModelLoader::Load(path.c_str()); break;
				case GameAssetKind::ModelMetadata: {
					std::string model = GetModelPathOfMetadata(path);
					if (FileManager::FileExists(model.c_str()))
						VoxelModelLoader::Load(model.c_str());
					break;
				}
				case GameAssetKind::Sound: {
					// The header is enough to tell a sound from garbage.
					std::unique_ptr<IAudioStream> stream(OpenAudioStream(path));
					break;
				}
				default: break;
			}
		}
	} // namespace client
} // namespace spades
