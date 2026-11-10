#include "AppPaths.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <cstdint>
#include <string>

namespace Hephaestus {

std::filesystem::path ExecutableDirectory()
{
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        DWORD result = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
        if (result == 0) {
            return {};
        }
        if (result < buffer.size() - 1) {
            buffer.resize(result);
            return std::filesystem::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
        if (buffer.size() > 32768) {
            return {};
        }
    }
#elif defined(__APPLE__)
    char buffer[4096];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        std::error_code ec;
        auto exePath = std::filesystem::canonical(buffer, ec);
        if (!ec) {
            return exePath.parent_path();
        }
    }
    return {};
#else
    std::error_code ec;
    auto exePath = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return exePath.parent_path();
    }
    return {};
#endif
}

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
