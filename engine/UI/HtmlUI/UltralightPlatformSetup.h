#pragma once

#include <string>

namespace Hephaestus::HtmlUI {

struct UltralightPlatformSetup {
  // Initializes Ultralight platform handlers (FileSystem, FontLoader, Logger).
  // Must be called before creating ultralight::Renderer.
  static void Init(const std::string& baseDir, const std::string& logFilePath);
};

} // namespace Hephaestus::HtmlUI
