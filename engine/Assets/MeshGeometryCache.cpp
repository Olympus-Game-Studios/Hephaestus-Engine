#include "MeshGeometryCache.h"

#include "ModelImporter.h"

#include <iostream>
#include <mutex>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#undef min
#undef max
#endif

namespace Hephaestus {

namespace {

struct Entry {
    std::shared_ptr<const MeshData> mesh;
    std::filesystem::file_time_type lastWrite{};
};

std::mutex& CacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, Entry>& Cache()
{
    static std::unordered_map<std::string, Entry> cache;
    return cache;
}

} // namespace

std::filesystem::path MeshGeometryCache::Resolve(const std::string& assetPath, const std::filesystem::path& projectRoot)
{
    namespace fs = std::filesystem;

    if (assetPath.empty()) {
        return {};
    }

    // Skip built-in resources
    if (assetPath.rfind("builtin://", 0) == 0) {
        return {};
    }

    fs::path path(assetPath);
    std::error_code ec;

    // Absolute path
    if (path.is_absolute() && fs::exists(path, ec)) {
        return path.lexically_normal();
    }

    auto tryRoots = [&](const fs::path& root) -> fs::path {
        if (root.empty()) {
            return {};
        }

        // root / assetPath
        fs::path candidate = (root / path).lexically_normal();
        if (fs::exists(candidate, ec)) {
            return candidate;
        }

        // root / assets / assetPath (handles passing paths without leading "assets/")
        candidate = (root / "assets" / path).lexically_normal();
        if (fs::exists(candidate, ec)) {
            return candidate;
        }

        return {};
    };

    // FIRST: Try the explicitly provided project root
    if (!projectRoot.empty()) {
        fs::path resolved = tryRoots(projectRoot);
        if (!resolved.empty()) {
            return resolved;
        }
    }

    auto tryWalkUp = [&](fs::path start) -> fs::path {
        fs::path cur = start;
        for (int depth = 0; depth < 10 && !cur.empty(); ++depth) {
            fs::path resolved = tryRoots(cur);
            if (!resolved.empty()) {
                return resolved;
            }
            if (!cur.has_parent_path()) {
                break;
            }
            cur = cur.parent_path();
        }
        return {};
    };

    // Try relative to CWD and parents
    {
        fs::path resolved = tryWalkUp(fs::current_path());
        if (!resolved.empty()) {
            return resolved;
        }
    }

    // Try relative to executable directory and parents
    fs::path exeDir;
#ifdef _WIN32
    std::wstring buffer(512, L'\0');
    while (true) {
        DWORD result = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (result == 0) {
            break;
        }
        if (result < buffer.size() - 1) {
            buffer.resize(result);
            exeDir = fs::path(buffer).parent_path();
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
#endif
    if (!exeDir.empty()) {
        fs::path resolved = tryWalkUp(exeDir);
        if (!resolved.empty()) {
            return resolved;
        }
    }

    // Last attempt: if there's an engine-like root with a "projects" folder, scan project roots.
    auto findProjectsRoot = [&](fs::path start) -> fs::path {
        fs::path cur = start;
        for (int depth = 0; depth < 10 && !cur.empty(); ++depth) {
            fs::path projects = (cur / "projects").lexically_normal();
            if (fs::exists(projects, ec) && fs::is_directory(projects, ec)) {
                return projects;
            }
            if (!cur.has_parent_path()) {
                break;
            }
            cur = cur.parent_path();
        }
        return {};
    };

    fs::path projectsRoot;
    projectsRoot = findProjectsRoot(fs::current_path());
    if (projectsRoot.empty() && !exeDir.empty()) {
        projectsRoot = findProjectsRoot(exeDir);
    }

    if (!projectsRoot.empty()) {
        for (const auto& entry : fs::directory_iterator(projectsRoot, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_directory(ec)) {
                continue;
            }
            fs::path projRoot = entry.path();
            fs::path resolved = tryRoots(projRoot);
            if (!resolved.empty()) {
                return resolved;
            }
        }
    }

    return {};
}

std::shared_ptr<const MeshData> MeshGeometryCache::Get(const std::string& meshAssetPath,
                                                       const std::filesystem::path& projectRoot)
{
    const std::filesystem::path meshPath = Resolve(meshAssetPath, projectRoot);
    if (meshPath.empty()) {
        return nullptr;
    }
    std::error_code ec;
    const std::string key = meshPath.lexically_normal().generic_string();
    auto lastWrite = std::filesystem::last_write_time(meshPath, ec);
    if (ec) {
        lastWrite = std::filesystem::file_time_type::min();
    }
    {
        std::lock_guard<std::mutex> lock(CacheMutex());
        auto it = Cache().find(key);
        if (it != Cache().end() && it->second.lastWrite == lastWrite) {
            return it->second.mesh;
        }
    }
    // Loaded outside the lock, so a slow import does not hold up other threads; two threads loading the same
    // changed file both succeed and the last one is kept.
    auto mesh = std::make_shared<MeshData>();
    std::string error;
    if (!ModelImporter::LoadModel(meshPath, *mesh, error)) {
        std::cerr << "[MeshGeometryCache] Failed to load '" << meshPath.generic_string() << "': " << error << "\n";
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(CacheMutex());
    Entry& entry = Cache()[key];
    entry.mesh = std::move(mesh);
    entry.lastWrite = lastWrite;
    return entry.mesh;
}

void MeshGeometryCache::Clear()
{
    std::lock_guard<std::mutex> lock(CacheMutex());
    Cache().clear();
}

} // namespace Hephaestus
