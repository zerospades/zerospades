# Vulkan Renderer — TODO

Open work only; what is done is in the git history. The GL renderer in
`Sources/Draw/OpenGL/` defines the visual result, not how Vulkan gets there.

## Critical

- [ ] **Longer freeze between the server list and the map loading view** (seen with
      `r_radiosity` 1). Leaving the title map joins its radiosity worker, whose batch
      of up to 128 chunks (GL: 8) cannot be interrupted: likely the bulk of it.
      Give both workers a stop flag checked between chunks, set before joining.
      The depth pre-pass adds four pipelines compiled at map load (cold cache on
      MoltenVK). Time `SetGameMap` and the two joins to confirm.

## Wrong results

- [ ] **`r_vsync` ignored by Vulkan** — the present mode always prefers MAILBOX (GL
      applies `r_vsync` through `SDL_GL_SetSwapInterval` only). Choose FIFO at 1,
      IMMEDIATE (else MAILBOX, else FIFO) at 0, and mark the swapchain stale when it
      changes. `cl_fps` is honoured as in GL (`SDLRunner`).
- [ ] **Swapchain extent compared against SDL's drawable size** — `PrepareSwapchain`
      rebuilds when they differ, but the swapchain follows `currentExtent`; where the
      two disagree for good it would rebuild every frame (not seen yet). Compare
      against `ChooseSwapchainExtent`.

## Spec / performance

- [ ] **Depth copied into an R32F colour image** (`CopyDepthForSampling`, the 1x path
      for post filters and SSAO): a depth-to-colour `vkCmdCopyImage` is not allowed
      by Vulkan 1.0 core (VUID-vkCmdCopyImage-srcImage-01548). Copy through a
      fragment or compute pass instead; also drop `SAMPLED` from the 1x mirror depth
      (the MoltenVK Intel issue the main depth avoids).
- [ ] **Redundant depth work with SSAO** — the mirror depth resolved twice under MSAA
      at `r_water` 3, the main depth copied twice at 1x; the depth-resolve filter
      makes framebuffers and sets every frame, and SSAO brings it to its pool limit
      of four.
- [ ] **Radiosity / ambient-shadow dispatch every frame** while chunks only wait for
      upload (`GetNumDirtyChunks` counts them); radiosity now uploads 64 chunks a
      frame against a batch of 128.
- [ ] **Depth barriers list only late fragment tests as their source** (attachment →
      shader read); add early fragment tests.

## GL features still missing

- [ ] **Temporal AA** (`r_temporalAA`, `GLTemporalAAFilter`). Plan: Halton sub-pixel
      jitter in the main view's projection (`VulkanSceneView`, not the mirror nor the
      shadow cascades); a compute resolve reprojecting through the depth and last
      frame's unjittered view-projection, neighbourhood-clamped in YCoCg, into two
      history images used in turn (one queue: barriers, no copies); first-person
      pixels (depth < `kFirstPersonDepthEnd`) treated as fixed to the screen;
      history reset on cuts, resizes and `r_scale` changes.
- [ ] **Model shadows under physical lighting** — `BasicMapPhys.frag` and
      `BasicModelVertexColorPhys.frag` don't sample the model-shadow cascades
      (`SceneLight/ModelShadow.glsl`); the basic shaders do.
- [ ] **Water mirror skipped when the water is out of sight** (GL `r_occlusionQuery`)
      — one occlusion query per frame in flight, reset outside the pass, read once
      that slot's fence has signalled (or `VK_EXT_conditional_rendering`).

## Known deltas from GL

- [ ] **Fog2 in-scatter dimmer than GL** — the flat `Sky.frag` fog-colour fill is
      still drawn under Fog2 to compensate. Re-check against GL now that depth is
      sampled through `sceneDepthSampleImage` (a D32 image read through `sampler2D`
      returned 0 on MoltenVK), then drop the workaround once Fog2 matches.
- [ ] **Bloom marginally sharper** — GL pre-blurs the first downsample level before
      building the pyramid; `VulkanBloomFilter` doesn't. Needs a visual A/B.

## Robustness / performance

- [ ] **Pipelines compile on first draw** — `PreloadShaders` only warms the SPIR-V
      blobs. Pre-create the pipelines at load (render passes are known then), so
      neither the first frame nor a map change stalls on compilation.
- [ ] **Audit load/store ops** — `LOAD` where `DONT_CARE` would do, `STORE` where
      the contents are never read again.
- [ ] **Outline tuning in the settings UI** — `r_outlinesDepthThreshold` and
      `r_outlinesStrength` (`VulkanCavityOutlineFilter.cpp`) are cvars only.

## Unscheduled ideas

Pipeline derivatives for the three water programs; merging compatible render
passes into subpasses; more transient targets through `VulkanTemporaryImagePool`;
indirect drawing, instancing and GPU culling for the map; async uploads on a
transfer queue.

## OpenGL and software renderers (found while porting)

- [ ] **Glare behind models in GL and SW** — Vulkan hides the flashlight glare
      behind anything the scene draws in front of the lamp, players included; GL and
      SW still apply the map-only rule of 93eaed00. GL's `Glare.vs` should test the
      scene's depth (first-person range excluded), SW its depth buffer; keep "drawn
      under the first-person view".
- [ ] **Lit particles at night in GL** — GL still blacks out unlit particles at
      night; Vulkan lights them per corner (`SpriteLight.glsl`): sun at the daylight,
      sky, minimum ambient, dynamic lights. Port that model to GL's sprite shaders.
- [ ] **GL radiosity / ambient-shadow worker races** — `GLRadiosityRenderer` and
      `GLAmbientShadowRenderer` share chunk state with their worker threads unguarded:
      `dirty` and the dirty ranges are written by map edits and taken by the worker
      without a lock (edits can be lost), the worker reads `GetSceneDef()` while the
      render thread rewrites it, and `transferDone.exchange(true)` hands the data back
      before it is copied out, so the next batch can rewrite it mid-upload. The
      Vulkan renderers' protocol (a mutex on the dirty state, the eye captured when a
      batch starts, ownership passed by `transferDone` only after the copy) applies
      as is.

## Notes

- The lit map and model fragment shaders bind 16 sampled images with SSAO: exactly
  the per-stage minimum Vulkan guarantees. Anything more they sample must free one
  first (e.g. the four radiosity volumes packed into fewer images).
- Push constants: 128 bytes at most, see `VulkanPushConstants.h`.
