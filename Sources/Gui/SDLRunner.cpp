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

#include <cctype>
#include <cstring>
#include <memory>

#include "SDLRunner.h"

#include <Gui/Utils/DisplayUtils.h>
#include <Gui/Utils/Icon.h>
#include "SDLGLDevice.h"
#include <Audio/ALDevice.h>
#include <Audio/NullDevice.h>
#include <Client/Client.h>
#include <Client/ScaledRenderer.h>
#include <Core/ConcurrentDispatch.h>
#include <Core/Debug.h>
#include <Core/Disposable.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/Math.h>
#include <Core/Settings.h>
#include <Draw/OpenGL/GLRenderer.h>
#include <Draw/SW/SWPort.h>
#include <Draw/SW/SWRenderer.h>
#include <ZeroSpades.h>

SPADES_SETTING(r_videoWidth);
SPADES_SETTING(r_videoHeight);
DEFINE_SPADES_SETTING(r_fullscreen, "0");
DEFINE_SPADES_SETTING(r_vsync, "1");
DEFINE_SPADES_SETTING(r_allowSoftwareRendering, "0");
DEFINE_SPADES_SETTING(r_renderer, "gl");
DEFINE_SPADES_SETTING(s_audioDriver, "openal");
DEFINE_SPADES_SETTING(cl_fps, "0");

static int lastMouseX = 0, lastMouseY = 0;

namespace spades {
	namespace gui {

		SDLRunner::SDLRunner()
		    : m_hasSystemMenu(false), m_uiScale(1.0F), m_scaledRenderer(nullptr) {}

		SDLRunner::~SDLRunner() {}

		client::IAudioDevice* SDLRunner::CreateAudioDevice() {
			if (EqualsIgnoringCase(s_audioDriver, "openal")) {
				return new audio::ALDevice();
			} else if (EqualsIgnoringCase(s_audioDriver, "null")) {
				return new audio::NullDevice();
			} else {
				SPLog("Unknown audio driver: %s, falling back to OpenAL", s_audioDriver.CString());
				s_audioDriver = "openal";
				return new audio::ALDevice();
			}
		}

		std::string SDLRunner::TranslateButton(Uint8 b) {
			SPADES_MARK_FUNCTION();
			switch (b) {
				case SDL_BUTTON_LEFT: return "LeftMouseButton";
				case SDL_BUTTON_RIGHT: return "RightMouseButton";
				case SDL_BUTTON_MIDDLE: return "MiddleMouseButton";
				case SDL_BUTTON_X1: return "MouseButton4";
				case SDL_BUTTON_X2: return "MouseButton5";
				default: return std::string();
			}
		}

		std::string SDLRunner::TranslateKey(const SDL_Keysym& k) {
			SPADES_MARK_FUNCTION();

			switch (k.sym) {
				case SDLK_ESCAPE: return "Escape";
				case SDLK_UP: return "Up";
				case SDLK_DOWN: return "Down";
				case SDLK_LEFT: return "Left";
				case SDLK_RIGHT: return "Right";
				case SDLK_PAGEUP: return "PageUp";
				case SDLK_PAGEDOWN: return "PageDown";
				case SDLK_SPACE: return " ";
				case SDLK_TAB: return "Tab";
				case SDLK_BACKSPACE: return "BackSpace";
				case SDLK_DELETE: return "Delete";
				case SDLK_RETURN: return "Enter";
				case SDLK_SLASH: return "/";
				case SDLK_KP_MINUS: return "-";
				case SDLK_KP_PLUS: return "+";
				case SDLK_KP_1: return "num1";
				case SDLK_KP_2: return "num2";
				case SDLK_KP_3: return "num3";
				case SDLK_KP_4: return "num4";
				case SDLK_KP_5: return "num5";
				case SDLK_KP_6: return "num6";
				case SDLK_KP_7: return "num7";
				case SDLK_KP_8: return "num8";
				case SDLK_KP_9: return "num9";
				case SDLK_KP_0: return "num0";
				default: return std::string(SDL_GetScancodeName(k.scancode));
			}
		}

		int SDLRunner::GetModState() { return SDL_GetModState(); }

		void SDLRunner::ProcessEvent(SDL_Event& event, View& view) {
			switch (event.type) {
				case SDL_QUIT:
					view.Closing();
					// running = false;
					break;
				case SDL_MOUSEBUTTONDOWN:
					view.KeyEvent(TranslateButton(event.button.button), true);
					break;
				case SDL_MOUSEBUTTONUP:
					view.KeyEvent(TranslateButton(event.button.button), false);
					break;
				case SDL_MOUSEMOTION:
					if (m_active) {
						if (view.NeedsAbsoluteMouseCoordinate())
							// window pixels -> the UI's virtual pixels
							view.MouseEvent(static_cast<float>(event.motion.x) / m_uiScale,
							                static_cast<float>(event.motion.y) / m_uiScale);
						else
							view.MouseEvent(static_cast<float>(event.motion.xrel), static_cast<float>(event.motion.yrel));
					}
					break;
				case SDL_MOUSEWHEEL:
					view.WheelEvent(static_cast<float>(-event.wheel.x), static_cast<float>(-event.wheel.y));
					break;
				case SDL_KEYDOWN:
					if (!event.key.repeat) {
						const SDL_Keysym& keysym = event.key.keysym;
						// Toggle fullscreen mode
						if (keysym.mod & KMOD_ALT && keysym.sym == SDLK_RETURN) {
							SDL_Window* window = SDL_GetWindowFromID(event.key.windowID);
							if (r_fullscreen) {
								if (!SDL_SetWindowFullscreen(window, 0))
									r_fullscreen = 0;
								else
									SPLog("Couldn't exit fullscreen mode: %s", SDL_GetError());
							} else {
								if (!SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN))
									r_fullscreen = 1;
								else
									SPLog("Couldn't enter fullscreen mode: %s", SDL_GetError());
							}
							return;
						}
						view.KeyEvent(TranslateKey(keysym), true);
					}
					break;
				case SDL_KEYUP: view.KeyEvent(TranslateKey(event.key.keysym), false); break;
				case SDL_TEXTINPUT: view.TextInputEvent(event.text.text); break;
				case SDL_TEXTEDITING:
					view.TextEditingEvent(event.edit.text, event.edit.start, event.edit.length);
					break;
				case SDL_WINDOWEVENT:
					if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
						// Only grab the mouse back if the view wants it grabbed: menus
						// use an absolute cursor. This matches the state the client
						// loop last set, so its cached mode stays in sync.
						SDL_SetRelativeMouseMode(view.NeedsAbsoluteMouseCoordinate() ? SDL_FALSE
						                                                               : SDL_TRUE);
						SDL_ShowCursor(SDL_DISABLE);
						m_active = true;
					} else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
						SDL_SetRelativeMouseMode(SDL_FALSE);
						SDL_ShowCursor(SDL_ENABLE);
						m_active = false;
					}
					if (event.window.event == SDL_WINDOWEVENT_MOVED) {
						// Remember the monitor the window was dragged onto, so the next
						// window (the game's, or the setup's on the next launch) opens there.
						SDL_Window* window = SDL_GetWindowFromID(event.window.windowID);
						if (window) {
							display::SetPreferredDisplay(display::GetWindowDisplay(window));
							WindowMoved(window);
						}
					}
					break;
				default: break;
			}
		}

		void SDLRunner::RunClientLoop(SDL_Window* wnd, spades::client::IRenderer* renderer,
									  spades::client::IAudioDevice* audio) {
			{
				Handle<View> view(CreateView(renderer, audio), false);
				Uint32 ot = SDL_GetTicks();

				bool running = true;
				bool lastShift = false;
				bool lastCtrl = false;
				bool editing = false;
				bool lastGui = false;
				bool lastAlt = false;
				bool absoluteMouseCoord = true;

				SPLog("Starting Client Loop");

				m_uiScale = renderer->UIScale();

				while (running) {
					SDL_Event event;

					DispatchQueue::GetThreadQueue()->ProcessQueue();

					Uint32 dt = SDL_GetTicks() - ot;

					if ((float)cl_fps != 0) {
						// Limit the frame rate
						Uint32 desiredDelay = static_cast<Uint32>(1000.0F / (float)cl_fps);
						desiredDelay = std::max<Uint32>(std::min<Uint32>(desiredDelay, 200), 1);
						if (dt < desiredDelay) {
							SDL_Delay(desiredDelay - dt);

							// Remeasure the time delta
							dt = SDL_GetTicks() - ot;
						}
					}

					ot += dt;
					if ((int32_t)dt > 0) {
						// The UI scale is settled here, once per frame, before the view
						// runs: the whole frame is laid out at one scale (a new one makes
						// the UI rebuild itself at the start of RunFrame).
						const bool rescaled = m_scaledRenderer && m_scaledRenderer->Update();
						if (rescaled)
							m_uiScale = renderer->UIScale();

						view->RunFrame((float)dt / 1000.0F);

						// The rebuilt UI's cursor still holds a position in the old
						// scale's units: re-send the real one, so hover and clicks line
						// up without waiting for the mouse to move.
						if (rescaled && absoluteMouseCoord && m_active) {
							int mx, my;
							SDL_GetMouseState(&mx, &my);
							view->MouseEvent(static_cast<float>(mx) / m_uiScale,
							                 static_cast<float>(my) / m_uiScale);
						}

						// Views draw; the runner is the only thing that presents. Present
						// before `RunFrameLate`, which is where post-frame work happens
						// (closing a subview, restoring renderer state) — that work must
						// never run between drawing the frame and swapping it.
						renderer->FrameDone();
						renderer->Flip();

						view->RunFrameLate((float)dt / 1000.0F);
					}

					if (view->WantsToBeClosed()) {
						view->Closing();
						SPLog("Close requested by Client");
						break;
					}

					int modState = GetModState();
					if (modState & KMOD_CTRL) {
						if (!lastCtrl) {
							view->KeyEvent("Control", true);
							lastCtrl = true;
						}
					} else {
						if (lastCtrl) {
							view->KeyEvent("Control", false);
							lastCtrl = false;
						}
					}

					if (modState & KMOD_SHIFT) {
						if (!lastShift) {
							view->KeyEvent("Shift", true);
							lastShift = true;
						}
					} else {
						if (lastShift) {
							view->KeyEvent("Shift", false);
							lastShift = false;
						}
					}

					if (modState & KMOD_GUI) {
						if (!lastGui) {
							view->KeyEvent("Meta", true);
							lastGui = true;
						}
					} else {
						if (lastGui) {
							view->KeyEvent("Meta", false);
							lastGui = false;
						}
					}

					if (modState & KMOD_ALT) {
						if (!lastAlt) {
							view->KeyEvent("Alt", true);
							lastAlt = true;
						}
					} else {
						if (lastAlt) {
							view->KeyEvent("Alt", false);
							lastAlt = false;
						}
					}

					bool ed = view->AcceptsTextInput();
					if (ed && !editing)
						SDL_StartTextInput();
					else if (!ed && editing)
						SDL_StopTextInput();
					editing = ed;

					if (editing) {
						// virtual pixels -> window pixels, for the IME candidate window
						AABB2 rt = view->GetTextInputRect();
						SDL_Rect srt;
						srt.x = (int)(rt.GetMinX() * m_uiScale);
						srt.y = (int)(rt.GetMinY() * m_uiScale);
						srt.w = (int)(rt.GetWidth() * m_uiScale);
						srt.h = (int)(rt.GetHeight() * m_uiScale);
						SDL_SetTextInputRect(&srt);
					}

					bool ab = view->NeedsAbsoluteMouseCoordinate();
					if (ab != absoluteMouseCoord) {
						if (ab) {
							SDL_SetRelativeMouseMode(SDL_FALSE);
							SDL_WarpMouseInWindow(wnd, lastMouseX, lastMouseY);
						} else {
							SDL_GetMouseState(&lastMouseX, &lastMouseY);

							// re-center if needed...
							if (lastMouseX == 0 && lastMouseY == 0) {
								int sw, sh;
								SDL_GetWindowSize(wnd, &sw, &sh);
								lastMouseX = sw / 2;
								lastMouseY = sh / 2;
							}

							SDL_SetRelativeMouseMode(SDL_TRUE);
						}

						absoluteMouseCoord = ab;
					}

					// Process events
					while (SDL_PollEvent(&event))
						ProcessEvent(event, *view);
				}

				SPLog("Leaving Client Loop");
			}
		}

		auto SDLRunner::GetRendererType() -> RendererType {
			if (EqualsIgnoringCase(r_renderer, "gl"))
				return RendererType::GL;
			else if (EqualsIgnoringCase(r_renderer, "sw"))
				return RendererType::SW;
			else
				SPRaise("Unknown renderer name: %s", r_renderer.CString());
		}

		class SDLSWPort : public draw::SWPort, public Disposable {
			SDL_Window* wnd;
			SDL_Surface* surface;
			bool adjusted;
			int actualW, actualH;

			Handle<Bitmap> framebuffer;

			void SetFramebufferBitmap() {
				if (adjusted) {
					framebuffer = Handle<Bitmap>::New(actualW, actualH);
				} else {
					framebuffer = Handle<Bitmap>::New(reinterpret_cast<uint32_t*>(surface->pixels),
													  surface->w, surface->h);
				}
			}

			void EnsureSurfaceIsValid() {
				if (!surface)
					SPRaise("The SDL surface associated with this SDLSWPart has already been"
							"destroyed.");
			}

		protected:
			~SDLSWPort() {
				if (surface && SDL_MUSTLOCK(surface))
					SDL_UnlockSurface(surface);
			}

			/** (Re)acquires the window surface; SDL replaces it when the window is resized. */
			void AcquireSurface() {
				surface = SDL_GetWindowSurface(wnd);
				if (!surface)
					SPRaise("SDL_GetWindowSurface failed: %s", SDL_GetError());
				// FIXME: check pixel format
				if (SDL_MUSTLOCK(surface))
					SDL_LockSurface(surface);
				actualW = surface->w & ~7;
				actualH = surface->h & ~7;
				if (actualW != surface->w || actualH != surface->h) {
					SPLog("Surface size %dx%d doesn't match the software renderer's"
						  " requirements. Rounded to %dx%d using an intermediate surface.",
						  surface->w, surface->h, actualW, actualH);
					adjusted = true;
					memset(surface->pixels, 0, surface->w * surface->h * 4);
				} else {
					adjusted = false;
				}
				SetFramebufferBitmap();
			}

		public:
			SDLSWPort(SDL_Window* wnd) : wnd(wnd), surface(nullptr) { AcquireSurface(); }

			void Dispose() override { surface = nullptr; }

			Bitmap& GetFramebuffer() override {
				EnsureSurfaceIsValid();

				return *framebuffer;
			}
			void Swap() override {
				EnsureSurfaceIsValid();

				if (adjusted) {
					int sy = (surface->h - actualH) >> 1;
					int sx = (surface->w - actualW) >> 1;
					uint32_t* outPixels = reinterpret_cast<uint32_t*>(surface->pixels);
					outPixels += sx + sy * (surface->pitch >> 2);

					uint32_t* inPixels = framebuffer->GetPixels();
					for (int y = 0; y < actualH; y++) {
						std::memcpy(outPixels, inPixels, actualW * 4);

						outPixels += surface->pitch >> 2;
						inPixels += actualW;
					}
				}

				if (SDL_MUSTLOCK(surface))
					SDL_UnlockSurface(surface);
				SDL_UpdateWindowSurface(wnd);

				// Resized: the old surface is gone; the next frame is drawn into a new
				// one of the new size (the renderer picks the change up).
				int ww = 0, wh = 0;
				SDL_GetWindowSize(wnd, &ww, &wh);
				if (ww > 0 && wh > 0 && (ww != surface->w || wh != surface->h)) {
					AcquireSurface();
					return;
				}

				if (SDL_MUSTLOCK(surface)) {
					SDL_LockSurface(surface);
					if (!adjusted)
						SetFramebufferBitmap();
				}
			}
		};

		std::tuple<Handle<client::IRenderer>, Handle<Disposable>>
		SDLRunner::CreateRenderer(SDL_Window* wnd, RendererType type) {
			switch (type) {
				case RendererType::GL: {
					auto glDevice = Handle<SDLGLDevice>::New(wnd).Cast<draw::IGLDevice>();
					auto dummy = Handle<Disposable>::New(); // FIXME
					return std::make_tuple(
					  Handle<draw::GLRenderer>::New(std::move(glDevice)).Cast<client::IRenderer>(),
					  std::move(dummy));
				}
				case RendererType::SW: {
					auto port = Handle<SDLSWPort>::New(wnd).Cast<draw::SWPort>();
					return std::make_tuple(
					  Handle<draw::SWRenderer>::New(port).Cast<client::IRenderer>(),
					  port.Cast<Disposable>());
				}
				default: SPRaise("Invalid renderer type");
			}
		}

		void SDLRunner::Run(int width, int height) {
			SPADES_MARK_FUNCTION();

			SDL_Init(SDL_INIT_VIDEO);
			display::MigrateSettings();

			try {
				std::string caption;
				{
					caption = PACKAGE_STRING;
#if !NDEBUG
					caption.append(" DEBUG build");
#endif
#ifndef GIT_COMMIT_HASH
#ifdef ZEROSPADES_COMPILER_STR
					caption.append(" " ZEROSPADES_COMPILER_STR); // add compiler to window title when git hash unavailable
#endif
#endif
				}

				auto rtype = GetRendererType();

				Uint32 sdlFlags = 0;
				switch (rtype) {
					case RendererType::GL:
						sdlFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
						SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
						SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
						SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
						if (!r_allowSoftwareRendering)
							SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
						break;
					case RendererType::SW: sdlFlags = 0; break;
				}

				if (!m_hasSystemMenu) {
					if (r_fullscreen)
						sdlFlags |= SDL_WINDOW_FULLSCREEN;
#ifdef __MACOSX__
					if (!r_fullscreen)
						sdlFlags |= SDL_WINDOW_BORDERLESS;
#endif
				}

				// Open on the monitor the setup window was last shown on. In fullscreen
				// mode SDL switches that monitor, not the primary one.
				int displayIndex = display::GetPreferredDisplay();
				const bool fullscreen = (sdlFlags & SDL_WINDOW_FULLSCREEN) != 0;
				float fixedScale = 0.0F;
				if (HasFixedLayout()) {
					// Laid out at width x height: enlarged for this monitor by the same
					// rule as the game's UI, so it is as legible.
					display::Resolution desktop;
					fixedScale = display::GetDesktopResolution(displayIndex, desktop)
					               ? client::ScaledRenderer::ComputeScale(
					                   static_cast<float>(desktop.width),
					                   static_cast<float>(desktop.height))
					               : 1.0F;
					width = static_cast<int>(std::lround(width * fixedScale));
					height = static_cast<int>(std::lround(height * fixedScale));
				} else {
					// The UI follows the window's size, so the window can have any.
					sdlFlags |= SDL_WINDOW_RESIZABLE;
					if (display::IsAutoResolution()) {
						display::Resolution desktop;
						if (fullscreen) {
							// The monitor's own mode, so going fullscreen switches none.
							if (display::GetDesktopResolution(displayIndex, desktop)) {
								width = desktop.width;
								height = desktop.height;
							}
						} else {
							// As large as the monitor allows: the window system fits a
							// maximized window between its taskbars, docks and frame.
							sdlFlags |= SDL_WINDOW_MAXIMIZED;
						}
					}
				}

				SDL_Window* window = SDL_CreateWindow(
				  caption.c_str(), SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),
				  SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), width, height, sdlFlags);

				if (!window) {
					std::string msg = SDL_GetError();
					SPRaise("Failed to create graphics window: %s", msg.c_str());
				}

#ifdef __APPLE__
#elif __unix
				SDL_Surface* icon = nullptr;
				SDL_RWops* icon_rw = nullptr;
				icon_rw = SDL_RWFromConstMem(g_appIconData, GetAppIconDataSize());
				if (icon_rw != nullptr) {
					icon = IMG_LoadPNG_RW(icon_rw);
					SDL_FreeRW(icon_rw);
				}
				if (icon == nullptr) {
					std::string msg = SDL_GetError();
					SPLog("Failed to load icon: %s", msg.c_str());
				} else {
					SDL_SetWindowIcon(window, icon);
					SDL_FreeSurface(icon);
				}
#endif

				SDL_SetRelativeMouseMode(SDL_FALSE);
				SDL_ShowCursor(SDL_DISABLE);
				m_active = true;

				{
					Handle<client::IRenderer> renderer;
					Handle<Disposable> windowReference;
					std::tie(renderer, windowReference) = CreateRenderer(window, rtype);

					// The UI and HUD are scaled with the window (or, for a fixed layout,
					// by the scale its window was enlarged by).
					auto scaled = Handle<client::ScaledRenderer>::New(renderer);
					if (fixedScale > 0.0F)
						scaled->SetFixedScale(fixedScale);
					m_scaledRenderer = scaled.GetPointerOrNull();
					renderer = std::move(scaled).Cast<client::IRenderer>();

					Handle<client::IAudioDevice> audio(CreateAudioDevice(), false);

					if (rtype == RendererType::GL) {
						// Vsync prefers its adaptive kind, which shows a frame that misses
						// the display's refresh at once, tearing, instead of holding it
						// to the next one and halving the frame rate. Where the platform
						// has none (SDL's macOS backend), plain vsync.
						const int vsync = r_vsync;
						const bool adaptive = vsync == 1 && SDL_GL_SetSwapInterval(-1) == 0;
						if (!adaptive && SDL_GL_SetSwapInterval(vsync) != 0)
							SPLog("Couldn't set the swap interval to %d: %s", vsync,
							      SDL_GetError());
						else
							SPLog("Swap interval: %s", adaptive ? "adaptive vsync"
							                           : vsync ? "vsync"
							                                   : "immediate");
					}

					WindowCreated(window);
					RunClientLoop(window, renderer.GetPointerOrNull(), audio.GetPointerOrNull());
					WindowDestroying(window);
					m_scaledRenderer = nullptr;

					// `SDL_Window` and its associated resources will be inaccessible
					// past this point. Some referencing objects might be still alive due to
					// the indeterministic nature of AngelScript's tracing GC, so we explicitly
					// break such references right now.
					windowReference->Dispose();
				}
			} catch (...) {
				SDL_Quit();
				throw;
			}

			SDL_Quit();
		}
	} // namespace gui
} // namespace spades
