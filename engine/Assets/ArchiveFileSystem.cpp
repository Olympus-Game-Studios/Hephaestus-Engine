#include "ArchiveFileSystem.h"

#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>

namespace Hephaestus {
namespace {

constexpr uint32_t kEndOfCentralDirectory = 0x06054b50u;
constexpr uint32_t kCentralHeader = 0x02014b50u;
constexpr uint32_t kLocalHeader = 0x04034b50u;
constexpr std::size_t kEndRecordSize = 22;
constexpr std::size_t kCentralHeaderSize = 46;
constexpr std::size_t kLocalHeaderSize = 30;
constexpr std::size_t kMaxCommentSize = 0xFFFF;
constexpr uint16_t kStored = 0;
constexpr uint16_t kDeflated = 8;
// Larger entries are rejected rather than inflated into memory.
constexpr uint32_t kMaxEntrySize = 1u << 30;

uint16_t U16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t U32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t Crc32(const uint8_t* data, std::size_t size) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::string Normalize(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
        if (c == '/' && (out.empty() || out.back() == '/')) {
            continue;
        }
        out.push_back(c);
    }
    return out;
}

bool ReadAt(std::ifstream& file, uint64_t offset, uint8_t* out, std::size_t size) {
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(size));
    return static_cast<std::size_t>(file.gcount()) == size;
}

bool UnsafeName(const std::string& name) {
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t end = name.find('/', start);
        const std::string part = name.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..") {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return name.find(':') != std::string::npos;
}

} // namespace

std::string ArchiveFileSystem::Key(const std::string& virtualPath) {
    std::string key = Normalize(virtualPath);
    while (!key.empty() && key.front() == '/') {
        key.erase(key.begin());
    }
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return key;
}

bool ArchiveFileSystem::Mount(const std::filesystem::path& archivePath, std::string& error) {
    std::ifstream file(archivePath, std::ios::binary);
    if (!file) {
        error = "cannot open archive " + archivePath.string();
        return false;
    }
    file.seekg(0, std::ios::end);
    const uint64_t fileSize = static_cast<uint64_t>(file.tellg());
    if (fileSize < kEndRecordSize) {
        error = archivePath.filename().string() + " is too small to be a ZIP archive";
        return false;
    }

    const std::size_t tailSize = static_cast<std::size_t>(std::min<uint64_t>(fileSize, kEndRecordSize + kMaxCommentSize));
    std::vector<uint8_t> tail(tailSize);
    if (!ReadAt(file, fileSize - tailSize, tail.data(), tailSize)) {
        error = "cannot read " + archivePath.filename().string();
        return false;
    }
    std::size_t endRecord = tailSize;
    for (std::size_t i = tailSize - kEndRecordSize + 1; i-- > 0;) {
        if (U32(&tail[i]) == kEndOfCentralDirectory) {
            endRecord = i;
            break;
        }
    }
    if (endRecord == tailSize) {
        error = archivePath.filename().string() + " has no ZIP end record";
        return false;
    }
    const uint8_t* end = &tail[endRecord];
    if (U16(end + 4) != 0 || U16(end + 6) != 0) {
        error = archivePath.filename().string() + " spans several disks, which is not supported";
        return false;
    }
    const uint16_t entryCount = U16(end + 10);
    const uint32_t directorySize = U32(end + 12);
    const uint32_t directoryOffset = U32(end + 16);
    if (entryCount == 0xFFFF || directorySize == 0xFFFFFFFFu || directoryOffset == 0xFFFFFFFFu) {
        error = archivePath.filename().string() + " is a ZIP64 archive, which is not supported";
        return false;
    }
    if (static_cast<uint64_t>(directoryOffset) + directorySize > fileSize) {
        error = archivePath.filename().string() + " has a central directory outside the file";
        return false;
    }

    std::vector<uint8_t> directory(directorySize);
    if (directorySize > 0 && !ReadAt(file, directoryOffset, directory.data(), directorySize)) {
        error = "cannot read the central directory of " + archivePath.filename().string();
        return false;
    }

    Archive archive;
    archive.path = archivePath;
    archive.fileSize = fileSize;
    std::size_t offset = 0;
    for (uint32_t i = 0; i < entryCount; ++i) {
        if (offset + kCentralHeaderSize > directory.size() || U32(&directory[offset]) != kCentralHeader) {
            error = archivePath.filename().string() + " has a damaged central directory";
            return false;
        }
        const uint8_t* header = &directory[offset];
        const uint16_t flags = U16(header + 8);
        const std::size_t nameLength = U16(header + 28);
        const std::size_t recordSize = kCentralHeaderSize + nameLength + U16(header + 30) + U16(header + 32);
        if (offset + recordSize > directory.size()) {
            error = archivePath.filename().string() + " has a damaged central directory";
            return false;
        }
        Entry entry;
        entry.method = U16(header + 10);
        entry.crc32 = U32(header + 16);
        entry.compressedSize = U32(header + 20);
        entry.uncompressedSize = U32(header + 24);
        entry.localHeaderOffset = U32(header + 42);
        entry.name = Normalize(std::string(reinterpret_cast<const char*>(header + kCentralHeaderSize), nameLength));
        offset += recordSize;

        if (entry.name.empty() || entry.name.back() == '/') {
            continue;
        }
        if ((flags & 1u) != 0) {
            error = archivePath.filename().string() + ": " + entry.name + " is encrypted, which is not supported";
            return false;
        }
        if (entry.compressedSize == 0xFFFFFFFFu || entry.uncompressedSize == 0xFFFFFFFFu ||
            entry.localHeaderOffset == 0xFFFFFFFFu) {
            error = archivePath.filename().string() + " has ZIP64 entries, which are not supported";
            return false;
        }
        if (UnsafeName(entry.name)) {
            error = archivePath.filename().string() + " has an unsafe path: " + entry.name;
            return false;
        }
        archive.entries.push_back(std::move(entry));
    }

    const std::size_t archiveIndex = m_Archives.size();
    for (std::size_t e = 0; e < archive.entries.size(); ++e) {
        m_Index[Key(archive.entries[e].name)] = Location{archiveIndex, e};
    }
    m_Archives.push_back(std::move(archive));
    m_FilesDirty = true;
    return true;
}

void ArchiveFileSystem::Clear() {
    m_Archives.clear();
    m_Index.clear();
    m_Files.clear();
    m_FilesDirty = false;
}

const std::vector<std::string>& ArchiveFileSystem::Files() const {
    if (m_FilesDirty) {
        m_Files.clear();
        m_Files.reserve(m_Index.size());
        for (const auto& [key, location] : m_Index) {
            m_Files.push_back(m_Archives[location.archive].entries[location.entry].name);
        }
        std::sort(m_Files.begin(), m_Files.end(),
                  [](const std::string& a, const std::string& b) { return Key(a) < Key(b); });
        m_FilesDirty = false;
    }
    return m_Files;
}

bool ArchiveFileSystem::Contains(const std::string& virtualPath) const {
    return m_Index.contains(Key(virtualPath));
}

bool ArchiveFileSystem::Read(const std::string& virtualPath, std::vector<uint8_t>& out, std::string& error) const {
    const auto found = m_Index.find(Key(virtualPath));
    if (found == m_Index.end()) {
        error = "no archive contains " + virtualPath;
        return false;
    }
    const Archive& archive = m_Archives[found->second.archive];
    const Entry& entry = archive.entries[found->second.entry];
    const std::string where = archive.path.filename().string() + ": " + entry.name;
    if (entry.method != kStored && entry.method != kDeflated) {
        error = where + " uses compression method " + std::to_string(entry.method) + ", which is not supported";
        return false;
    }
    if (entry.uncompressedSize > kMaxEntrySize || entry.compressedSize > kMaxEntrySize) {
        error = where + " is too large to read";
        return false;
    }

    std::ifstream file(archive.path, std::ios::binary);
    uint8_t local[kLocalHeaderSize];
    if (!file || !ReadAt(file, entry.localHeaderOffset, local, kLocalHeaderSize) || U32(local) != kLocalHeader) {
        error = where + " has a damaged local header";
        return false;
    }
    const uint64_t dataOffset = static_cast<uint64_t>(entry.localHeaderOffset) + kLocalHeaderSize +
                                U16(local + 26) + U16(local + 28);
    if (dataOffset + entry.compressedSize > archive.fileSize) {
        error = where + " runs past the end of the archive";
        return false;
    }
    std::vector<uint8_t> compressed(entry.compressedSize);
    if (entry.compressedSize > 0 && !ReadAt(file, dataOffset, compressed.data(), compressed.size())) {
        error = "cannot read " + where;
        return false;
    }

    if (entry.method == kStored) {
        if (entry.compressedSize != entry.uncompressedSize) {
            error = where + " is stored with mismatched sizes";
            return false;
        }
        out = std::move(compressed);
    } else {
        int length = 0;
        char* inflated = stbi_zlib_decode_malloc_guesssize_headerflag(
            reinterpret_cast<const char*>(compressed.data()), static_cast<int>(compressed.size()),
            static_cast<int>(std::max<uint32_t>(entry.uncompressedSize, 1)), &length, 0);
        if (!inflated) {
            error = where + " could not be inflated";
            return false;
        }
        const bool sizeMatches = static_cast<uint32_t>(length) == entry.uncompressedSize;
        if (sizeMatches) {
            out.assign(reinterpret_cast<uint8_t*>(inflated), reinterpret_cast<uint8_t*>(inflated) + length);
        }
        std::free(inflated);
        if (!sizeMatches) {
            error = where + " inflated to the wrong size";
            return false;
        }
    }
    if (Crc32(out.data(), out.size()) != entry.crc32) {
        out.clear();
        error = where + " failed its CRC check";
        return false;
    }
    return true;
}

} // namespace Hephaestus
