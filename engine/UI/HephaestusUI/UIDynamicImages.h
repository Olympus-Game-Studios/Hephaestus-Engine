#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hephaestus::HephaestusUI {

/**
 * Images whose pixels are supplied at runtime (a playing video, a captured view) rather than read from a file.
 * An image element names one by its id like any other image; both UI backends (the packaged Vulkan renderer and the
 * editor's Play overlay) look an id up here before resolving it as a file, and re-upload when its version changes.
 *
 * One process-wide store, because the producer (the script host) and the backends do not share an owner; access is
 * locked so a producer may update from any thread.
 */
class UIDynamicImages {
public:
    struct Image {
        std::vector<uint8_t> rgba; // width * height * 4, rows top to bottom
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t version = 0;      // bumped by every Set
    };

    static UIDynamicImages& Instance();

    /** Publishes (or replaces) the pixels of `id`. */
    void Set(const std::string& id, const uint8_t* rgba, uint32_t width, uint32_t height);
    void Remove(const std::string& id);
    /** The image's version, or 0 when there is no such image (versions start at 1). */
    uint64_t Version(const std::string& id) const;
    /** Copies the image out when its version differs from `knownVersion`; false when unchanged or missing. */
    bool CopyIfNewer(const std::string& id, uint64_t knownVersion, Image& out) const;

private:
    mutable std::mutex m_Mutex;
    std::unordered_map<std::string, Image> m_Images;
    uint64_t m_NextVersion = 1;
};

} // namespace Hephaestus::HephaestusUI
