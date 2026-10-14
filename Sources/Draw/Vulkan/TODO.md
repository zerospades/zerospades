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
