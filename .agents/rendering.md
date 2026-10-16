# Rendering and Vulkan

The frame renderer is shared by GLFW and Qt editor paths; editor-only gizmos, outlines, and related GPU resources are compile-time/host-specific. Verify current target definitions and macros before moving rendering code or assuming a feature is shared with `HephaestusGame`.

- Preserve the ownership and lifetime ordering of `VulkanDevice`, `VulkanSwapchain`, `EngineFrameRenderer`, and resources allocated through VMA. The frame renderer requires the initialized device and swapchain to outlive it.
- Treat swapchain recreation and viewport resizing as resource lifecycle events. Inspect dependent render passes, framebuffers, image views, extent-dependent pipelines/resources, synchronization, and image layouts when changing these paths.
- Respect Vulkan object dependencies and existing cleanup order. Check whether descriptor-set layouts or other handles are owned or borrowed before destroying them; use existing ownership flags and conventions.
- Keep GPU/CPU synchronization and upload behavior consistent with the existing frame flow. Avoid introducing waits or allocations on hot paths without a measured or correctness-based need.
- Preserve both GLFW-window and native-surface initialization paths. Qt editor surface handling and packaged GLFW runtime are separate host adaptations.
- Runtime HUI is rendered through the shared frame renderer's Vulkan overlay path. For changes affecting that contract, also read `.agents/runtime.md` and verify the packaged path when feasible.

Read `.agents/architecture.md`, `.agents/cpp.md`, and `.agents/testing.md` for subsystem ownership, C++ lifetime rules, and host-appropriate checks.
