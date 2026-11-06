# Vulkan Renderer — TODO

Open work only; what is done is in the git history. The GL renderer in
`Sources/Draw/OpenGL/` defines the visual result, not how Vulkan gets there.

## Wrong results

- [ ] **Depth copies wrong under `D24_UNORM_S8_UINT`** — the 1x path copies the depth
      aspect bit for bit into an R32F image (`CopyDepthForSampling`), which only reads
      as depth for `D32_SFLOAT_S8_UINT`. A GPU without that format falls back to D24,
      whose packed 24-bit integers then read as garbage floats. Convert in a fragment
      or compute pass there, or refuse Vulkan in setup without D32S8.

## Known deltas from GL

- [ ] **Fog2 in-scatter dimmer than GL** — the flat `Sky.frag` fog-colour fill is
      still drawn under Fog2 to compensate. Re-check against GL now that depth is
      sampled through `sceneDepthSampleImage` (a D32 image read through `sampler2D`
      returned 0 on MoltenVK), then drop the workaround once Fog2 matches.

## Renderer contract from `autodetect-screen-res` (UI scale, high DPI)

That branch scales the UI with the window and renders high-DPI windows at full
resolution. Once it lands, Vulkan must follow the same `IRenderer` contract the GL
renderer does there:

- [ ] **2D in window units, framebuffer in pixels** — `ScreenWidth`/`ScreenHeight`
      return the window size (`SDL_GetWindowSize`, points on a high-DPI display), the
      units of mouse input; the swapchain stays at `SDL_Vulkan_GetDrawableSize`.
      `VulkanImageRenderer` currently projects 2D through the swapchain extent
      (pixels): project through `ScreenWidth`/`ScreenHeight` instead, with the
      viewport still covering the whole swapchain image. Convert clip rects
      (`BeginClippingRect`) to pixels for the scissor; full-screen 2D blits keep a
      pixel viewport and a quad in 2D units.
- [ ] **`ScreenPixelRatio()`** — swapchain pixels per window unit. FTFont, the pie
      menu's anti-aliasing and the crosshair size themselves with it (through
      `ScaledRenderer`, which multiplies in the UI scale).
- [ ] **High-DPI window** — `SDLRunner` adds `SDL_WINDOW_ALLOW_HIGHDPI` for GL only;
      add it for `RendererType::Vulkan` (the macOS bundle already declares
      `NSHighResolutionCapable`).
- [ ] **Resizes and density changes** — the game window is now resizable and may
      open maximized, so its size changes right after creation; it can also move to a
      display of another density. Re-read both sizes and recreate the swapchain on
      every change (`RecreateSwapchainDependencies`), before the next frame's 2D.
- [ ] **`CreateImageWithoutMipmaps`** — an image created with `mipLevels = 1` (the
      6x8 pixel font's enlarged atlases); the default falls back to `CreateImage`,
      whose mip chain blurs that font at fractional scales.

## Unscheduled ideas

Pipeline derivatives for the three water programs; merging compatible render
passes into subpasses; more transient targets through `VulkanTemporaryImagePool`;
indirect drawing, instancing and GPU culling for the map; async uploads on a
transfer queue; resolving the mirror's multisampled depth at `r_water` 3 through
`VK_KHR_depth_stencil_resolve` instead of storing it whole for a resolve pass.

## Notes

- The lit map and model fragment shaders bind 16 sampled images with SSAO: exactly
  the per-stage minimum Vulkan guarantees. Anything more they sample must free one
  first (e.g. the four radiosity volumes packed into fewer images).
- Push constants: 128 bytes at most, see `VulkanPushConstants.h`.
