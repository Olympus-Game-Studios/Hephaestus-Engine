#pragma once

class QWindow;

namespace Hephaestus::QtEditor {

// Returns the CAMetalLayer owned by a native macOS Qt window.
void* GetMacMetalLayer(QWindow* window);

} // namespace Hephaestus::QtEditor
