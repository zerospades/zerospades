# Vulkan renderer: conventions

Rules every part of the Vulkan renderer follows. [TODO.md](TODO.md) tracks what is
still to do.

## Push constants: 128 bytes, the same on every device

A push constant block may hold at most **128 bytes**
(`kPortablePushConstantsSize`, [VulkanPushConstants.h](VulkanPushConstants.h)).
That is the minimum Vulkan guarantees for `maxPushConstantsSize`, so a block that
fits it works on every device.

**Why it is not the limit the device reports.** A block's size is compiled into
its shader's SPIR-V and its pipeline layout, so it cannot change with the device
at run time. Checking against the device's own limit would also pass on the GPUs
that report more (MoltenVK reports 4096) and fail only on players' GPUs that
report the minimum. Held to 128, a block that would not fit everywhere is refused
on every machine, including the developer's.

**What goes where.**

- Push constants hold small per-draw values: a model's matrix and colours, a
  chunk's origin, a filter pass's parameters.
- Anything larger, or shared by a whole pass, goes in a uniform buffer. For
  example, `VulkanSceneView` holds a pass's view and is bound once with a dynamic
  offset per view.

**How it is enforced.**

- `VulkanProgram` refuses any program whose reflected push constant block is
  larger than 128 bytes.
- A pipeline layout's range and the draws' pushes are sized from one and the same
  type, never separately. A range that disagrees with what is pushed drops the
  tail on some drivers and draws nothing correctly on others (the long-sprite and
  particle bug on AMD).
- New code builds the range with `PushConstantRange<T>()` and pushes with
  `PushConstants(..., const T&)`, which check the size at compile time. Older
  code uses `sizeof` of the struct on both sides, with a `static_assert` beside
  the struct against `kPortablePushConstantsSize`.
- A vec3 in a push block is aligned to 16 bytes (std430) but holds 12, so the
  C++ structs follow each vec3 with a float (a value or padding) to keep their
  offsets the same as the shader's.

**If a shader ever needs more.** Should a shader measurably gain from a larger
block on the devices that allow one, compile two variants from the one source: the
block in push constants, or in a uniform buffer. Choose between them at pipeline
creation from `maxPushConstantsSize`, and feed both from the same C++ struct.
Never declare a block larger than 128 bytes on its own.
