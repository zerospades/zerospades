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

#pragma once

#include <map>
#include <string>
#include <utility>

#include "IAudioChunk.h"
#include "IImage.h"
#include "IModel.h"
#include "Skin.h"
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class IAudioDevice;
		class IRenderer;

		/**
		 * The assets of one game: every model, image and sound the game loads goes
		 * through here, by way of the renderer and audio device `CreateRenderer` and
		 * `CreateAudioDevice` return. Each file is loaded once, shared, and freed
		 * with the game.
		 *
		 * The software's own assets (the menus, the loading screen, fonts) are
		 * loaded by the base renderer and audio device instead.
		 */
		class GameResources : public RefCountedObject {
		public:
			GameResources(Handle<IRenderer> renderer, Handle<IAudioDevice> audio);

			/**
			 * Loads `path` once and shares it. With a `skin` that has its own `path`,
			 * the skin's is loaded, once, and shared by that skin's players.
			 */
			Handle<IImage> RegisterImage(const std::string& path, Skin* skin = nullptr);
			Handle<IModel> RegisterModel(const std::string& path, Skin* skin = nullptr);
			Handle<IAudioChunk> RegisterSound(const std::string& path, Skin* skin = nullptr);

			/**
			 * A renderer that draws with the base one and loads through this set,
			 * seeing `skin` on top if there is one.
			 */
			Handle<IRenderer> CreateRenderer(Handle<Skin> skin = {});
			/**
			 * An audio device that plays with the base one and loads through this
			 * set, seeing `skin` on top if there is one.
			 */
			Handle<IAudioDevice> CreateAudioDevice(Handle<Skin> skin = {});

			/** Loads every asset of `skin`, so wearing it loads nothing. Raises on the first that fails. */
			void Preload(Skin& skin);

			/**
			 * Checks the game asset at `path` loads, without making anything of it,
			 * and raises if it doesn't. Models and images are read whole; a sound
			 * only by its header, as decoding it all would cost too much here, so a
			 * sound broken past its header only fails when it is first loaded.
			 * Needs no renderer or audio device.
			 */
			static void CheckFile(const std::string& path);

		protected:
			~GameResources();

		private:
			struct Cache {
				std::map<std::string, Handle<IImage>> images;
				std::map<std::string, Handle<IModel>> models;
				std::map<std::string, Handle<IAudioChunk>> sounds;
			};

			Handle<IRenderer> renderer;
			Handle<IAudioDevice> audio;
			Cache shared;
			// The files skins have their own of, by skin: a skin loaded again under
			// the same name is another skin.
			std::map<const Skin*, std::pair<Handle<Skin>, Cache>> skinned;

			/** Where `path`, loaded for `skin`, is held. */
			Cache& GetCache(Skin* skin, const std::string& path);
		};
	} // namespace client
} // namespace spades
