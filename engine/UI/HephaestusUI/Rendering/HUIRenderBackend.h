#pragma once

#include "../UIRenderer.h"
#include "../UIStyle.h"

#include <string>
#include <vector>

namespace Hephaestus::HephaestusUI {

/**
 * HUIRenderBackend.h
 *
 * Renderer-independent interface for consuming the HUI draw command list.
 *
 * The HUI widget tree, layout, and command generation never depend on the
 * concrete backend; a backend only receives the finished command stream plus
 * viewport/project context and is responsible for turning it into pixels.
 *
 * Concrete backends:
 *   - VulkanHUIRenderer  -> packaged game runtime (Vulkan)
 *   - (editor)           -> QPainter overlay (RuntimeUIOverlayWidget) renders
 *                           the same command stream for Editor Play
 *
 * The per-frame contract is:
 *   1. SetDrawCommands(commands)   - provide the latest command list
 *   2. PrepareFrame()              - convert commands to geometry, upload
 *                                    any new textures/atlas data (no Vulkan
 *                                    recording allowed in this call)
 *   3. (backend-specific) Record   - emit vkCmd* recording inside the render
 *                                    pass; kept out of this interface so it
 *                                    stays API-neutral
 *   4. ClearDrawCommands()         - release the command reference
 */
class IHUIRenderBackend {
public:
    virtual ~IHUIRenderBackend() = default;

    IHUIRenderBackend(const IHUIRenderBackend&) = delete;
    IHUIRenderBackend& operator=(const IHUIRenderBackend&) = delete;

    /** Set the logical viewport (render target) size in pixels. */
    virtual void SetViewportSize(const UIVec2& viewportSize) = 0;

    /** Base directory for resolving relative asset paths (fonts, images). */
    virtual void SetProjectRoot(const std::string& projectRoot) = 0;

    /** Provide the draw commands to render. The caller must keep the vector alive until the frame is submitted. */
    virtual void SetDrawCommands(const std::vector<UIDrawCommand>& commands) = 0;

    /** Drop the command reference (called at the end of each frame). */
    virtual void ClearDrawCommands() = 0;

    /** Convert the current commands into geometry and flush pending GPU uploads. */
    virtual void PrepareFrame() = 0;

protected:
    IHUIRenderBackend() = default;
};

} // namespace Hephaestus::HephaestusUI