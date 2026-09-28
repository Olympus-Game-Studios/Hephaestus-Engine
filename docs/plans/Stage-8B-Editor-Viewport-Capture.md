# Stage 8B editor observation / capture implementation notes

The editor's live `EditorContext` owns selection and scene persistence. Automation
uses `SelectEntities`/`ClearSelection` (then synchronizes the Outliner) and
`SaveSceneAs`/`SaveScene`; there is no separate selection or scene serializer.
`scene.save_as` accepts only project-relative paths; lexical parent traversal,
absolute paths, and symlink escapes are rejected before the editor serializer
runs. An absent `.hpscene` extension is appended as in the editor Save As UI.
The active scene path and dirty flag are updated by `EditorContext` on success.
`scene.new` and `scene.open` reuse `EditorContext` and refuse to discard a dirty
scene; the latter uses project-relative paths and the normal deserializer and
mesh-library rebuild. Persistent scene operations are disabled in Play Mode.
The `OpenDocumentRegistry` currently tracks asset editors, not the main scene.

The renderer's main render pass ends with its color attachment in
`PRESENT_SRC_KHR`. Its HUI draw call occurs at the end of that pass, after the
editor gizmo/outlines. `VulkanSwapchain` queries
`VkSurfaceCapabilitiesKHR::supportedUsageFlags` on each creation and adds
`TRANSFER_SRC` only when supported. A requested capture records a
color-attachment-write → transfer-read barrier after the pass, copies the
presentable image to a VMA host-visible staging buffer, and restores the
present layout before queue presentation. The submitted frame's fence is
polled without blocking the editor; the staging allocation is invalidated and
copied to CPU memory before its fence can be reused. Both RGBA8 and BGRA8
swapchain formats are converted to RGBA before `stb_image_write` PNG encoding.
Resize cancels pending requests and prevents stale swapchain image use;
shutdown waits for outstanding work before destroying the readback buffer.

Captures go under `<project>/.hephaestus/automation/captures/` (the existing
generated-data directory). This is not an asset. The capture tool takes no
arbitrary output path. It captures the Vulkan viewport including overlays
drawn into that render pass, but no Qt/desktop chrome.

**Portability limitation:** the renderer has no separate final-color target.
If a platform's surface does not support `TRANSFER_SRC` for swapchain images,
the capture request fails explicitly; an intermediate final-color/present path
would be required to support that surface. No unsupported swapchain usage is
ever requested.
