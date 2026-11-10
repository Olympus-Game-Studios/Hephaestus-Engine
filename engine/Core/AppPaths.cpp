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
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t* localAppData = nullptr;
    std::filesystem::path root;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        root = std::filesystem::path(localAppData) / appName;
    }
    CoTaskMemFree(localAppData);
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return root;
#else
    (void)appName;
    return {};
#endif
}

std::filesystem::path UserDataFor(const std::filesystem::path& appName)
{
#ifdef _WIN32
    return LocalAppDataFor(appName);
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return home ? std::filesystem::path(home) / "Library" / "Application Support" / appName : std::filesystem::path{};
#else
    if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data) {
        return std::filesystem::path(data) / appName;
    }
    const char* home = std::getenv("HOME");
    return home ? std::filesystem::path(home) / ".local" / "share" / appName : std::filesystem::path{};
#endif
}

} // namespace Hephaestus
