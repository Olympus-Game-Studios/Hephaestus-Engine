#include "UltralightPlatformSetup.h"

#include <AppCore/Platform.h>
#include <Ultralight/platform/Platform.h>
#include <Ultralight/platform/Config.h>

namespace Hephaestus::HtmlUI {

void UltralightPlatformSetup::Init(const std::string& baseDir,
                                  const std::string& logFilePath) {
  auto& platform = ultralight::Platform::instance();

  ultralight::Config config;
  // Resource/file loading happens via FileSystem. This baseDir is where
  // file:/// URLs will resolve.
  platform.set_config(config);

  platform.set_font_loader(ultralight::GetPlatformFontLoader());
  platform.set_file_system(ultralight::GetPlatformFileSystem(baseDir.c_str()));
  platform.set_logger(ultralight::GetDefaultLogger(logFilePath.c_str()));
}

} // namespace Hephaestus::HtmlUI
