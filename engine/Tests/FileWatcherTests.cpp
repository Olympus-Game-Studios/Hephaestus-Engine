#include "Assets/AssetDatabase.h"
#include "Assets/FileWatcher.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    std::cout << "[PASS] " << message << "\n";
    return true;
}

void WriteFile(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

bool WaitFor(const std::atomic<int>& value, int target, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (value.load() >= target) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return value.load() >= target;
}

} // namespace

int main()
{
    bool ok = true;
    const fs::path root = fs::temp_directory_path() / "HephaestusFileWatcherTests";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "assets" / "textures");
    const fs::path source = root / "assets" / "textures" / "brick.png";
    WriteFile(source, "first");

    {
        AssetDatabase db;
        db.Load(root);
        db.GetOrCreateForSource(source, AssetType::Texture, "texture", "1", "default");

        std::atomic<int> changes{0};
        FileWatcher watcher;
        watcher.SetDebounce(std::chrono::milliseconds(50));
        watcher.SetStabilityWait(std::chrono::milliseconds(50));
        watcher.Start(&db, [&](const std::string&, const fs::path&) { ++changes; });

        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        ok &= Check(changes.load() == 0, "existing files are not reported as changed on the first scan");

        WriteFile(source, "second, longer contents");
        ok &= Check(WaitFor(changes, 1, std::chrono::seconds(5)), "an edited source file is reported");

        fs::remove(source, ec);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        ok &= Check(changes.load() == 1, "a deleted source file is not reported for reimport");
        watcher.Stop();
    }

    fs::remove_all(root, ec);
    return ok ? 0 : 1;
}
