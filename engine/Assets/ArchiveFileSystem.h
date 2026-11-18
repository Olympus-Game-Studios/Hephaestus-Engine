#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hephaestus {

/**
 * Read only ZIP archives (PK3 included) merged into one virtual namespace.
 * Stored and deflated entries are supported; ZIP64, encryption and spanning
 * are rejected. Lookups ignore case. When two mounts hold the same path the
 * later mount wins.
 */
class ArchiveFileSystem {
public:
    /// Indexes the archive's central directory. Nothing is read until a file is.
    bool Mount(const std::filesystem::path& archivePath, std::string& error);
    void Clear();

    std::size_t MountCount() const { return m_Archives.size(); }
    /// Every virtual path, sorted, spelled as the winning archive stores it.
    const std::vector<std::string>& Files() const;
    bool Contains(const std::string& virtualPath) const;
    bool Read(const std::string& virtualPath, std::vector<uint8_t>& out, std::string& error) const;

private:
    struct Entry {
        std::string name;
        uint32_t localHeaderOffset = 0;
        uint32_t compressedSize = 0;
        uint32_t uncompressedSize = 0;
        uint32_t crc32 = 0;
        uint16_t method = 0;
    };
    struct Archive {
        std::filesystem::path path;
        uint64_t fileSize = 0;
        std::vector<Entry> entries;
    };
    struct Location {
        std::size_t archive = 0;
        std::size_t entry = 0;
    };

    static std::string Key(const std::string& virtualPath);

    std::vector<Archive> m_Archives;
    std::unordered_map<std::string, Location> m_Index;
    mutable std::vector<std::string> m_Files;
    mutable bool m_FilesDirty = false;
};

} // namespace Hephaestus
