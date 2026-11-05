#include "AppPaths.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace Hephaestus {

std::filesystem::path LocalAppDataFor(const std::filesystem::path& appName)
{
#ifdef _WIN32
    wchar_t* localAppData = nullptr;
    std::filesystem::path root;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        root = std::filesystem::path(localAppData) / appName;
    }
    CoTaskMemFree(localAppData);
    return root;
#else
    (void)appName;
    return {};
#endif
}

} // namespace Hephaestus
