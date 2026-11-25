#include "UIDynamicImages.h"

#include <cstring>

namespace Hephaestus::HephaestusUI {

UIDynamicImages& UIDynamicImages::Instance() {
    static UIDynamicImages store;
    return store;
}

void UIDynamicImages::Set(const std::string& id, const uint8_t* rgba, uint32_t width, uint32_t height) {
    if (id.empty() || !rgba || width == 0 || height == 0) return;
    const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
    std::lock_guard<std::mutex> lock(m_Mutex);
    Image& image = m_Images[id];
    image.rgba.resize(bytes);
    std::memcpy(image.rgba.data(), rgba, bytes);
    image.width = width;
    image.height = height;
    image.version = m_NextVersion++;
}

void UIDynamicImages::Remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Images.erase(id);
}

uint64_t UIDynamicImages::Version(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto it = m_Images.find(id);
    return it == m_Images.end() ? 0 : it->second.version;
}

bool UIDynamicImages::CopyIfNewer(const std::string& id, uint64_t knownVersion, Image& out) const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto it = m_Images.find(id);
    if (it == m_Images.end() || it->second.version == knownVersion) return false;
    out = it->second;
    return true;
}

} // namespace Hephaestus::HephaestusUI
