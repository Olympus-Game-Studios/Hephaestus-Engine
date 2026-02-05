#pragma once

/**
 * @file NativeSurface.h
 * @brief Platform-agnostic native window surface abstraction.
 *
 * This provides a minimal seam that allows the renderer to attach to either:
 * - A GLFW window (runtime/legacy editor path)
 * - A Qt QWindow (Qt editor path)
 * 
 * The renderer creates a Vulkan surface from the native handle without
 * knowing the underlying windowing system.
 */

#include <cstdint>

namespace Hephaestus {

/**
 * @brief Native window surface descriptor.
 *
 * Contains platform-specific handles needed to create a Vulkan surface.
 * On Windows: windowHandle = HWND, displayHandle = nullptr (unused)
 * On Linux/X11: windowHandle = Window, displayHandle = Display*
 * On macOS: windowHandle = CAMetalLayer*, displayHandle = nullptr
 */
struct NativeSurface {
    void* windowHandle = nullptr;   // Platform window handle (HWND on Windows)
    void* displayHandle = nullptr;  // Display connection (X11 Display*, nullptr on Win32)
    uint32_t width = 0;             // Current surface width in pixels
    uint32_t height = 0;            // Current surface height in pixels
    
    /**
     * @brief Check if the surface descriptor is valid.
     * @return true if windowHandle is set and dimensions are non-zero.
     */
    bool IsValid() const {
        return windowHandle != nullptr && width > 0 && height > 0;
    }
};

} // namespace Hephaestus

