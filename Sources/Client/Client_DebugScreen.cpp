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
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "Client.h"

#include <Core/CpuID.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Imports/SDL.h>
#include <ZeroSpades.h>

#include "DemoNetClient.h"
#include "Fonts.h"
#include "GameMap.h"
#include "IFont.h"
#include "NetClient.h"
#include "SceneDefinition.h"
#include "Teamplay.h"
#include "Weapon.h"
#include "World.h"

// Platform headers last, so their macros cannot reach the project headers above.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

DEFINE_SPADES_SETTING(cg_debugSreenSmallFont, "0");

namespace spades {
	namespace client {

		/** One line of the debug screen. An empty `text` is a gap between two groups. */
		struct Client::DebugScreenLine {
			std::string text;
			Vector4 color;
			DebugScreenLine() : color(MakeVector4(1, 1, 1, 1)) {}
		};

		namespace {
			std::string DBGFormat(const char* format, ...) {
				char buf[512];
				va_list args;
				va_start(args, format);
				vsnprintf(buf, sizeof(buf), format, args);
				va_end(args);
				return std::string(buf);
			}

			/** The CPUID brand string comes padded with spaces. */
			std::string TrimBrand(const std::string& brand) {
				const std::size_t first = brand.find_first_not_of(' ');
				if (first == std::string::npos)
					return "Unknown";
				const std::size_t last = brand.find_last_not_of(' ');
				return brand.substr(first, last - first + 1);
			}

#if defined(__APPLE__)
			/** The CPU brand as macOS reports it, or an empty string when it does not tell. */
			std::string ReadMacCpuBrand() {
				char buf[256];
				buf[0] = 0;
				size_t size = sizeof(buf);
				if (sysctlbyname("machdep.cpu.brand_string", buf, &size, nullptr, 0) != 0)
					return std::string();
				buf[sizeof(buf) - 1] = 0;
				return std::string(buf);
			}
#endif

			/** Information about the machine, which cannot change while the game runs. It is
			 * gathered on the first call and kept. */
			const std::vector<std::string>& GetSystemInfoLines() {
				static std::vector<std::string> lines;
				static bool gathered = false;
				if (gathered)
					return lines;
				gathered = true;

				SDL_version linked;
				SDL_GetVersion(&linked);
				const char* videoDriver = SDL_GetCurrentVideoDriver();
				lines.push_back(DBGFormat("SDL: %d.%d.%d (%s)", (int)linked.major,
											(int)linked.minor, (int)linked.patch,
											videoDriver ? videoDriver : "unknown"));

				CpuID cpuid;
				std::string brand = TrimBrand(cpuid.GetBrand());
#if defined(__APPLE__)
				// CpuID only knows x86, so on Apple Silicon the brand is "Unknown".
				if (brand == "Unknown") {
					const std::string macBrand = TrimBrand(ReadMacCpuBrand());
					if (macBrand != "Unknown")
						brand = macBrand;
				}
#endif
				lines.push_back(DBGFormat("CPU: %dx %s", SDL_GetCPUCount(), brand.c_str()));

				return lines;
			}

			const char* GetToolName(Player::ToolType tool) {
				switch (tool) {
					case Player::ToolSpade: return "Spade";
					case Player::ToolBlock: return "Block";
					case Player::ToolWeapon: return "Weapon";
					case Player::ToolGrenade: return "Grenade";
				}
				return "Unknown";
			}

			/** The memory the process occupies in RAM, in bytes, or `0` when this platform does not
			 * tell. Asks the system. */
			std::uint64_t ReadProcessMemoryBytes() {
#if defined(_WIN32)
				// Looked up at run time so that no extra library has to be linked.
				typedef BOOL(WINAPI * GetProcessMemoryInfoFn)(HANDLE, PROCESS_MEMORY_COUNTERS*, DWORD);
				HMODULE kernel = GetModuleHandleA("kernel32.dll");
				if (!kernel)
					return 0;
				GetProcessMemoryInfoFn getInfo = reinterpret_cast<GetProcessMemoryInfoFn>(
				  reinterpret_cast<void*>(GetProcAddress(kernel, "K32GetProcessMemoryInfo")));
				if (!getInfo)
					return 0;
				PROCESS_MEMORY_COUNTERS counters;
				counters.cb = sizeof(counters);
				if (!getInfo(GetCurrentProcess(), &counters, sizeof(counters)))
					return 0;
				return static_cast<std::uint64_t>(counters.WorkingSetSize);
#elif defined(__APPLE__)
				mach_task_basic_info info;
				mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
				if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) !=
				    KERN_SUCCESS)
					return 0;
				return static_cast<std::uint64_t>(info.resident_size);
#elif defined(__linux__)
				FILE* file = fopen("/proc/self/statm", "r");
				if (!file)
					return 0;
				unsigned long long totalPages = 0, residentPages = 0;
				const int matched = fscanf(file, "%llu %llu", &totalPages, &residentPages);
				fclose(file);
				if (matched != 2)
					return 0;
				return static_cast<std::uint64_t>(residentPages) *
				       static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
#else
				return 0;
#endif
			}

			/** `ReadProcessMemoryBytes`, asked at most once a second. */
			std::uint64_t GetProcessMemoryBytes() {
				static std::uint64_t cached = 0;
				static bool hasRead = false;
				static Stopwatch lastReadTimer;
				if (hasRead && lastReadTimer.GetTime() < 1.0)
					return cached;
				hasRead = true;
				lastReadTimer.Reset();
				cached = ReadProcessMemoryBytes();
				return cached;
			}

			const char* GetCameraModeName(ClientCameraMode mode) {
				switch (mode) {
					case ClientCameraMode::None: return "None";
					case ClientCameraMode::NotJoined: return "Not joined";
					case ClientCameraMode::Free: return "Free";
					case ClientCameraMode::FirstPersonLocal: return "First person (local)";
					case ClientCameraMode::ThirdPersonLocal: return "Third person (local)";
					case ClientCameraMode::FirstPersonFollow: return "First person (follow)";
					case ClientCameraMode::ThirdPersonFollow: return "Third person (follow)";
				}
				return "Unknown";
			}
		} // namespace

		void Client::BuildDebugScreenLeft(std::vector<DebugScreenLine>& lines, Player* player) {
			const Vector4 white = MakeVector4(1, 1, 1, 1);
			const Vector4 gray = MakeVector4(0.7F, 0.7F, 0.7F, 1);

			auto addLine = [&lines](const std::string& text, const Vector4& color) {
				DebugScreenLine line;
				line.text = text;
				line.color = color;
				lines.push_back(line);
			};
			auto addGap = [&lines]() {
				if (!lines.empty() && !lines.back().text.empty())
					lines.push_back(DebugScreenLine());
			};

			// performance
			{
				const double fps = fpsCounter.GetFps();
				const double ups = upsCounter.GetFps();
				std::string perf;
				if (fps <= 0.0)
					perf = "FPS: NA";
				else
					perf = DBGFormat("%.0f FPS (%.1f ms)", fps, 1000.0 / fps);
				if (ups <= 0.0)
					perf += ", UPS: NA";
				else
					perf += DBGFormat(", %.0f UPS", ups);
				addLine(perf, white);

				// CPU time spent submitting the previous frame. With vsync on, the frame time
				// above includes the wait for the display, and this does not.
				addLine(DBGFormat("Draw: scene %.2f ms, 2D %.2f ms", debugDrawSceneMs, debugDraw2DMs), white);
			}

			// --- world
			addGap();
			{
				const Handle<GameMap>& map = world->GetMap();
				if (map)
					addLine(DBGFormat("Map size: %dx%dx%d", map->Width(), map->Height(),
					                map->Depth()),
					    white);
				addLine(DBGFormat("World time: %.1f s", world->GetTime()), white);
				addLine(DBGFormat("Players: %llu/%llu, grenades: %llu",
				                (unsigned long long)world->GetNumPlayers(),
				                (unsigned long long)world->GetNumPlayerSlots(),
				                (unsigned long long)world->GetAllGrenades().size()), white);
				addLine(DBGFormat("Entities: %llu local, %llu corpses",
				                (unsigned long long)localEntities.size(),
				                (unsigned long long)corpses.size()), white);
			}

			// the player whose view is drawn
			addGap();
			if (player) {
				Player& p = *player;
				addLine(DBGFormat("Player: %s (#%d), team: %s",
					p.GetName().c_str(), p.GetId(), p.GetTeamName().c_str()), white);

				const Vector3 pos = p.GetPosition();
				addLine(DBGFormat("XYZ: %.3f %.3f %.3f", pos.x, pos.y, pos.z), white);
				const IntVector3 block = pos.Floor();
				addLine(DBGFormat("Block: %d %d %d", block.x, block.y, block.z), white);
				const Vector3 eye = p.GetEye();
				addLine(DBGFormat("Eye: %.3f %.3f %.3f", eye.x, eye.y, eye.z), white);

				const Vector3 vel = p.GetVelocity();
				addLine(DBGFormat("Velocity: %.3f %.3f %.3f (horizontal %.3f)",
					vel.x, vel.y, vel.z, vel.GetLength2D()), white);

				auto addFlag = [](std::string& list, bool on, const char* name, const char* separator) {
					if (!on)
						return;
					if (!list.empty())
						list += separator;
					list += name;
				};

				// state of the player
				const bool grounded = p.IsOnGroundOrWade();
				std::string state;
				addFlag(state, !p.IsAlive(), "dead", ", ");
				addFlag(state, p.IsSpectator(), "spectator", ", ");
				addFlag(state, p.GetWade(), "wade", ", ");
				addFlag(state, grounded, "grounded", ", ");
				addFlag(state, !grounded, "airborne", ", ");
				addFlag(state, p.IsZoomed(), "zoomed", ", ");
				addLine("State: " + (state.empty() ? std::string("none") : state), white);

				// player inputs and weapon inputs
				const PlayerInput input = p.GetInput();
				const WeaponInput weapInput = p.GetWeaponInput();
				std::string keys;
				addFlag(keys, input.moveForward, "forward", ", ");
				addFlag(keys, input.moveBackward, "backward", ", ");
				addFlag(keys, input.moveLeft, "left", ", ");
				addFlag(keys, input.moveRight, "right", ", ");
				addFlag(keys, input.jump, "jump", ", ");
				addFlag(keys, input.crouch, "crouch", ", ");
				addFlag(keys, input.sneak, "sneak", ", ");
				addFlag(keys, input.sprint, "sprint", ", ");
				addFlag(keys, weapInput.primary, "primary", ", ");
				addFlag(keys, weapInput.secondary, "secondary", ", ");
				addLine("Input: " + (keys.empty() ? std::string("none") : keys), white);

				std::string tool = GetToolName(p.GetTool());
				if (p.IsToolWeapon())
					tool += " (" + p.GetWeapon().GetName() + ")";
				addLine("Tool: " + tool, white);

				if (p.IsLocalPlayer()) {
					Weapon& weapon = p.GetWeapon();
					addLine(DBGFormat("HP: %d, ammo: %d/%d%s", p.GetHealth(), weapon.GetAmmo(),
						weapon.GetStock(), weapon.IsReloading() ? " (reloading)" : ""), white);
					addLine(DBGFormat("Grenades: %d, blocks: %d", p.GetNumGrenades(), p.GetNumBlocks()), white);
					const IntVector3 bc = p.GetBlockColor();
					addLine(DBGFormat("Block color: %d %d %d (#%02X%02X%02X)", bc.x, bc.y, bc.z,
						bc.x & 0xFF, bc.y & 0xFF, bc.z & 0xFF), white);
				}
			} else {
				addLine("Player: none", white);
			}

			// the camera that is drawn
			addGap();
			{
				addLine(DBGFormat("Camera: %s", GetCameraModeName(GetCameraMode())), white);

				// The drawn view origin, so it is right for a free, a followed or a
				// third-person camera too.
				const Vector3 camPos = lastSceneDef.viewOrigin;
				addLine(DBGFormat("Camera XYZ: %.3f %.3f %.3f", camPos.x, camPos.y, camPos.z), white);

				// Yaw and pitch are read from the drawn view, so they are right for a free or
				// a followed camera too. Positive pitch looks up (z grows downwards).
				const Vector3 front = lastSceneDef.viewAxis[2].Normalize();
				const float pitch = RAD2DEG(asinf(Clamp(-front.z, -1.0F, 1.0F)));

				// bearing against the north the server announced
				float bearing = std::fmod(teamplay->GetBearing(MakeVector2(front.x, front.y)), 360.0F);
				if (bearing < 0.0F)
					bearing += 360.0F;

				const std::string labels[8] = {
					_Tr("Client", "N"),  _Tr("Client", "NE"), _Tr("Client", "E"),
					_Tr("Client", "SE"), _Tr("Client", "S"), _Tr("Client", "SW"),
					_Tr("Client", "W"), _Tr("Client", "NW"),
				};
				const int index = static_cast<int>(std::lround(bearing / 45.0F)) % 8;
				addLine(DBGFormat("Facing: %s (%.1f %.1f)", labels[index].c_str(), bearing, pitch), white);
			}

			// connection
			addGap();
			if (IsDemoMode()) {
				addLine(DBGFormat("Demo: %.1f/%.1f s, %.2fx%s", demoNet->GetTime(),
				                demoNet->GetDuration(), demoNet->GetSpeed(),
				                demoNet->IsPaused() ? " (paused)" : ""), white);
			} else {
				const std::string server =
				  serverName.empty() ? hostname.ToString(false) : serverName;
				addLine("Server: " + server, white);
				if (!gameMode.empty())
					addLine("Mode: " + gameMode + (world->IsModeFFA() ? " (FFA)" : ""), white);

				const char* protocol = "unknown";
				switch (hostname.GetProtocolVersion()) {
					case ProtocolVersion::v075: protocol = "0.75"; break;
					case ProtocolVersion::v076: protocol = "0.76"; break;
					default: break;
				}
				addLine(DBGFormat("Protocol: %s", protocol), white);

				// protocol extensions the server announced (never any in a demo)
				const std::vector<ServerExtensionInfo> extensions = GetServerExtensions();
				if (!extensions.empty()) {
					addLine(DBGFormat("Extensions: %llu", (unsigned long long)extensions.size()), white);
					for (const ServerExtensionInfo& ext : extensions) {
						addLine(DBGFormat("%s v%d%s", GetExtensionDisplayName(ext.id).c_str(),
							(int)ext.version, ext.supportedByClient ? "" : " (not supported)"),
								ext.supportedByClient ? white : gray);
					}
				}
			}
		}

		void Client::BuildDebugScreenRight(std::vector<DebugScreenLine>& lines, Player* player) {
			const Vector4 white = MakeVector4(1, 1, 1, 1);

			auto addLine = [&lines](const std::string& text, const Vector4& color) {
				DebugScreenLine line;
				line.text = text;
				line.color = color;
				lines.push_back(line);
			};
			auto addGap = [&lines]() {
				if (!lines.empty() && !lines.back().text.empty())
					lines.push_back(DebugScreenLine());
			};

			// version
			std::string version = PACKAGE_STRING;
#if !NDEBUG
				version += " DEBUG build";
#endif
				addLine(version, white);

			// machine
			for (const std::string& line : GetSystemInfoLines())
				addLine(line, white);

			{
				const unsigned long long usedMB = GetProcessMemoryBytes() / (1024ULL * 1024ULL);
				const int totalMB = SDL_GetSystemRAM();

				if (usedMB > 0 && totalMB > 0) {
					addLine(DBGFormat("Memory: %.0f%% %llu/%d MB",
						usedMB * 100.0 / totalMB, usedMB, totalMB), white);
				} else if (usedMB > 0) {
					addLine(DBGFormat("Memory: %llu MB", usedMB), white);
				} else if (totalMB > 0) {
					addLine(DBGFormat("Memory: ?/%d MB", totalMB), white);
				}
			}

			// renderer
			addGap();
			{
				const int sw = static_cast<int>(renderer->ScreenWidth());
				const int sh = static_cast<int>(renderer->ScreenHeight());

				const RendererInfo info = renderer->GetRendererInfo();
				if (!info.gpu.empty())
					addLine("GPU: " + info.gpu, white);

				if (!info.apiVersion.empty())
					addLine(DBGFormat("Renderer: %s (%s)", info.backend.c_str(), info.apiVersion.c_str()), white);
				else
					addLine(DBGFormat("Renderer: %s", info.backend.c_str()), white);

				if (!info.vendor.empty())
					addLine(DBGFormat("Display: %dx%d (%s)", sw, sh, info.vendor.c_str()), white);
				else
					addLine(DBGFormat("Display: %dx%d", sw, sh), white);

				if (info.renderWidth > 0 && info.renderHeight > 0)
					addLine(DBGFormat("Render size: %dx%d", info.renderWidth, info.renderHeight), white);

				if (!info.vsync.empty()) {
					addLine(DBGFormat("VSync: %s", info.vsync.c_str()), white);
				} else if (info.backend == "OpenGL") {
					// The GL renderer presents through the SDL GL context, which
					// knows its swap interval; other renderers report their own.
					const int interval = SDL_GL_GetSwapInterval();
					addLine(DBGFormat("VSync: %s", (interval == 0)
						? "off" : ((interval > 0) ? "on" : "adaptive")),  white);
				}
			}

			// what the center of the screen points at
			addGap();
			{
				const Vector3 origin = lastSceneDef.viewOrigin;
				const Vector3 direction = lastSceneDef.viewAxis[2].Normalize();
				const World::WeaponRayCastResult ray =
				  world->WeaponRayCast(origin, direction, ViewExcludedPlayerId());

				if (!ray.hit) {
					addLine(DBGFormat("Looking at: fog"), white);

					const IntVector3 col = world->GetFogColor();
					addLine(DBGFormat("Color: %d %d %d (#%02X%02X%02X)", col.x, col.y, col.z,
						col.x & 0xFF, col.y & 0xFF, col.z & 0xFF), white);
					addLine(DBGFormat("Distance: %.2f", (float)FOG_DISTANCE), white);
				} else if (ray.playerId) {
					stmp::optional<Player&> maybeTarget = world->GetPlayer(*ray.playerId);
					if (maybeTarget)
						addLine(DBGFormat("Looking at: player"), white);

					/*
					if (maybeTarget) {
						Player& target = maybeTarget.value();
						addLine(DBGFormat("Looking at: player %s (#%d)",
							target.GetName().c_str(), target.GetId()), white);
						addLine(DBGFormat("Team: %s", target.GetTeamName().c_str()), white);
					} else {
						addLine(DBGFormat("Looking at: player #%d", *ray.playerId), white);
					}

					std::string part;
					auto addPart = [&part](bool on, const char* name) {
						if (!on)
							return;
						if (!part.empty())
							part += ", ";
						part += name;
					};
					addPart((ray.hitFlag & hit_Head) ? true : false, "head");
					addPart((ray.hitFlag & hit_Torso) ? true : false, "torso");
					addPart((ray.hitFlag & hit_Arms) ? true : false, "arms");
					addPart((ray.hitFlag & hit_Legs) ? true : false, "legs");
					if (!part.empty())
						addLine("Hit: " + part, white);

					const float distance = (ray.hitPos - origin).GetLength();
					addLine(DBGFormat("Distance: %.2f", distance), white);
					*/
				} else {
					const IntVector3 pos = ray.blockPos;
					const float distance = (ray.hitPos - origin).GetLength();
					addLine(DBGFormat("Looking at: block %d %d %d", pos.x, pos.y, pos.z), white);

					const IntVector3 col = IntVectorFromColor(
					  world->GetMap()->GetColorWrapped(pos.x, pos.y, pos.z));
					addLine(DBGFormat("Color: %d %d %d (#%02X%02X%02X)", col.x, col.y, col.z,
					                col.x & 0xFF, col.y & 0xFF, col.z & 0xFF), white);
					addLine(DBGFormat("Distance: %.2f%s", distance,
						ray.startSolid ? " (inside a block)" : ""), white);
				}
			}
		}

		void Client::DrawDebugScreen() {
			SPADES_MARK_FUNCTION();

			if (!world)
				return;

			// The player whose eyes are drawn, if any. A free camera has none.
			Player* player = nullptr;
			switch (GetCameraMode()) {
				case ClientCameraMode::FirstPersonLocal:
				case ClientCameraMode::ThirdPersonLocal:
				case ClientCameraMode::FirstPersonFollow:
				case ClientCameraMode::ThirdPersonFollow: {
					stmp::optional<Player&> maybePlayer =
					  world->GetPlayer(GetCameraTargetPlayerId());
					if (maybePlayer)
						player = &maybePlayer.value();
					break;
				}
				default: break;
			}

			std::vector<DebugScreenLine> left;
			std::vector<DebugScreenLine> right;
			BuildDebugScreenLeft(left, player);
			BuildDebugScreenRight(right, player);

			const float sw = renderer->ScreenWidth();
			const float sh = renderer->ScreenHeight();

			IFont& guiFont = fontManager->GetGuiFont();
			IFont& smallFont = fontManager->GetSmallFont();
			IFont& font = cg_debugSreenSmallFont ? smallFont : guiFont;

			const float margin = 2.0F; // distance from the screen edge
			const float padX = 3.0F; // space between a line's background and its text
			const float padY = 1.0F;
			const float lineH = font.Measure("Ag").y + padY * 2.0F;

			const Vector4 white = MakeVector4(1, 1, 1, 1);
			const Vector4 gray = MakeVector4(0.7F, 0.7F, 0.7F, 1);
			const Vector4 background = MakeVector4(0, 0, 0, 0.5F);
			const Vector4 outline = MakeVector4(0, 0, 0, 0.8F);
			const Vector4 border = MakeVector4(0.6F, 0.6F, 0.6F, 0.6F);
			const Vector4 guide = MakeVector4(1, 1, 1, 0.35F);

			// The graphs run along the bottom edge, each with a title above it. The two
			// columns of text stop above them.
			const float graphH = 48.0F;
			const float graphTop = sh - margin - graphH - 1.0F;
			const float titleY = graphTop - lineH - 2.0F;
			const float columnsBottom = titleY - margin;

			auto textBoxWidth = [&](const std::string& text) {
				return font.Measure(text).x + padX * 2.0F;
			};
			auto drawTextBox = [&](const std::string& text, float x, float y,
			                       const Vector4& color) {
				renderer->SetColorAlphaPremultiplied(background);
				renderer->DrawFilledRect(x, y, x + textBoxWidth(text), y + lineH);
				font.Draw(text, MakeVector2(floorf(x + padX), floorf(y + padY)), 1.0F, color);
			};

			auto drawColumn = [&](const std::vector<DebugScreenLine>& lines, bool alignRight) {
				float y = margin;
				for (const DebugScreenLine& line : lines) {
					// A gap is half a line, and has no background.
					if (line.text.empty()) {
						y += lineH * 0.5F;
						continue;
					}
					if (y + lineH > columnsBottom)
						break;

					const float x =
					  alignRight ? floorf(sw - margin - textBoxWidth(line.text)) : margin;
					drawTextBox(line.text, x, y, line.color);
					y += lineH;
				}
			};

			drawColumn(left, false);
			drawColumn(right, true);

			// draw graphs

			// The smallest of `steps` that holds `peak`, so a graph keeps a steady scale until
			// the data outgrows it.
			auto pickCeiling = [](float peak, const float* steps, int count) {
				for (int i = 0; i < count; i++) {
					if (peak <= steps[i])
						return steps[i];
				}
				return steps[count - 1];
			};

			// The background of a graph at `x`, with its title above it.
			auto drawGraphBackground = [&](float x, float width, const std::string& title) {
				font.DrawOutline(title, MakeVector2(floorf(x), floorf(titleY + padY)), 1.0F, white, outline);
				renderer->SetColorAlphaPremultiplied(background);
				renderer->DrawFilledRect(x, graphTop, x + width, graphTop + graphH);
			};
			auto drawGraphBorder = [&](float x, float width) {
				renderer->SetColorAlphaPremultiplied(border);
				renderer->DrawOutlinedRect(x - 1.0F, graphTop - 1.0F, x + width + 1.0F,
				                           graphTop + graphH + 1.0F);
			};
			// A horizontal guide at `value`, when the graph's scale reaches that high.
			auto drawGuide = [&](float x, float width, float value, float ceiling,
			                     const char* label) {
				if (value >= ceiling)
					return;
				const float y = floorf(graphTop + graphH - value / ceiling * graphH);
				renderer->SetColorAlphaPremultiplied(guide);
				renderer->DrawFilledRect(x, y, x + width, y + 1.0F);

				const float labelH = font.Measure(label).y;
				const float labelY = (y - labelH >= graphTop) ? y - labelH : y + 1.0F;
				font.Draw(label, MakeVector2(floorf(x + 2.0F), floorf(labelY)), 1.0F, gray);
			};
			// A bar growing from the bottom of the graph.
			auto drawBar = [&](float x, float barW, float value, float ceiling,
			                   const Vector4& color) {
				const float h = std::min(value / ceiling, 1.0F) * graphH;
				if (h <= 0.0F)
					return;
				renderer->SetColorAlphaPremultiplied(color);
				renderer->DrawFilledRect(x, graphTop + graphH - h, x + barW, graphTop + graphH);
			};

			// Frame time. The darker part of each bar is the CPU time spent drawing, the rest is
			// waiting (vsync, or the GPU catching up).
			{
				const int count = kDebugFrameSamples;
				const float width = static_cast<float>(count);
				const float graphX = margin;

				float sum = 0.0F, low = 0.0F, high = 0.0F;
				int valid = 0;
				for (int i = 0; i < count; i++) {
					const float ms = debugFrames.At(i).frameMs;
					if (ms <= 0.0F)
						continue;
					low = (valid == 0) ? ms : std::min(low, ms);
					high = (valid == 0) ? ms : std::max(high, ms);
					sum += ms;
					valid++;
				}
				const float average = (valid > 0) ? sum / static_cast<float>(valid) : 0.0F;

				static const float steps[] = {10.0F, 20.0F, 40.0F, 80.0F, 160.0F};
				const float ceiling = pickCeiling(high, steps, 5);

				drawGraphBackground(graphX, width,
					DBGFormat("frame: min %.1f, avg %.1f, max %.1f ms", low, average, high));
				for (int i = 0; i < count; i++) {
					const DebugFrameSample& s = debugFrames.At(i);
					if (s.frameMs <= 0.0F)
						continue;

					// 60 FPS or better is green, 30 FPS or better yellow, anything slower red.
					Vector4 color = MakeVector4(1.0F, 0.35F, 0.35F, 1.0F);
					if (s.frameMs <= 17.5F)
						color = MakeVector4(0.35F, 0.9F, 0.35F, 1.0F);
					else if (s.frameMs <= 34.0F)
						color = MakeVector4(1.0F, 0.85F, 0.25F, 1.0F);

					const float x = graphX + static_cast<float>(i);
					drawBar(x, 1.0F, s.frameMs, ceiling, color);
					drawBar(x, 1.0F, s.cpuMs, ceiling,
					        MakeVector4(color.x * 0.5F, color.y * 0.5F, color.z * 0.5F, 1.0F));
				}
				drawGuide(graphX, width, 1000.0F / 60.0F, ceiling, "60 FPS");
				drawGuide(graphX, width, 1000.0F / 30.0F, ceiling, "30 FPS");
				drawGraphBorder(graphX, width);
			}

			// small cross at the center of the screem
			{
				const float cx = floorf(sw * 0.5F);
				const float cy = floorf(sh * 0.5F);
				const float arm = 5.0F;
				renderer->SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 0.8F));
				renderer->DrawFilledRect(cx - arm, cy, cx + arm + 1.0F, cy + 1.0F);
				renderer->DrawFilledRect(cx, cy - arm, cx + 1.0F, cy + arm + 1.0F);
			}
		}
	} // namespace client
} // namespace spades
