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
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <string>
#include <tuple>

#include <Imports/SDL.h>

#include <Core/Disposable.h>
#include <Core/IRunnable.h>
#include <Core/ServerAddress.h>

namespace spades {
	namespace client {
		class IRenderer;
		class ScaledRenderer;
		class IAudioDevice;
	} // namespace client
	namespace gui {
		class View;
		class SDLRunner {
			bool m_active;
			bool m_hasSystemMenu;
			/** Window units per UI unit (IRenderer::UIScale), for mouse positions. */
			float m_uiScale;
			/** The renderer's UI scaling, while a window is open; weak. */
			client::ScaledRenderer* m_scaledRenderer;

		protected:
			enum class RendererType { GL, SW };

			virtual RendererType GetRendererType();

			std::string TranslateKey(const SDL_Keysym&);
			std::string TranslateButton(Uint8 b);
			virtual int GetModState();
			void ProcessEvent(SDL_Event& event, View&);
			virtual void RunClientLoop(SDL_Window*, client::IRenderer* renderer,
			                           client::IAudioDevice* dev);
			virtual View* CreateView(client::IRenderer* renderer, client::IAudioDevice* dev) = 0;
			virtual client::IAudioDevice* CreateAudioDevice();
			std::tuple<Handle<client::IRenderer>, Handle<Disposable>>
			CreateRenderer(SDL_Window*, RendererType type);
			/**
			 * A view laid out at a fixed size (the size given to Run), like the setup
			 * window, instead of one that follows the window's size: its window is
			 * enlarged for the monitor, and not resizable.
			 */
			virtual bool HasFixedLayout() { return false; }
			/** Called once the window exists, before the view is created. */
			virtual void WindowCreated(SDL_Window*) {}
			/** The window was moved (possibly onto another monitor). */
			virtual void WindowMoved(SDL_Window*) {}
			/** Called after the client loop ends, while the window still exists. */
			virtual void WindowDestroying(SDL_Window*) {}

		public:
			SDLRunner();
			virtual ~SDLRunner();
			void Run(int width, int height);
			void SetHasSystemMenu(bool b) { m_hasSystemMenu = b; }
		};
	} // namespace gui
} // namespace spades