#include "CompiledMaterialShader.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace Hephaestus {
namespace {

constexpr char kMagic[4] = {'H', 'E', 'M', 'S'};
constexpr std::uint32_t kMaxTextures = 64;
constexpr std::uint32_t kMaxStringBytes = 4096;
constexpr std::uint32_t kMaxSpirvWords = 16u * 1024u * 1024u;

class Reader {
public:
    explicit Reader(const std::vector<char>& bytes) : m_bytes(bytes) {}

    bool U32(std::uint32_t& value) { return Raw(&value, sizeof(value)); }
    bool U64(std::uint64_t& value) { return Raw(&value, sizeof(value)); }
    bool String(std::string& value) {
        std::uint32_t size = 0;
        if (!U32(size) || size > kMaxStringBytes || Remaining() < size) return false;
        value.assign(m_bytes.data() + m_offset, size);
        m_offset += size;
        return true;
    }
    bool Words(std::vector<std::uint32_t>& value) {
        std::uint32_t count = 0;
        if (!U32(count) || count > kMaxSpirvWords || Remaining() / sizeof(std::uint32_t) < count) return false;
        value.resize(count);
        return Raw(value.data(), count * sizeof(std::uint32_t));
    }
    bool AtEnd() const { return m_offset == m_bytes.size(); }

private:
    size_t Remaining() const { return m_bytes.size() - m_offset; }
    bool Raw(void* out, size_t size) {
        if (Remaining() < size) return false;
        if (size > 0) std::memcpy(out, m_bytes.data() + m_offset, size);
        m_offset += size;
        return true;
    }

    const std::vector<char>& m_bytes;
    size_t m_offset = 0;
};

void PutU32(std::string& out, std::uint32_t value) { out.append(reinterpret_cast<const char*>(&value), sizeof(value)); }

void PutWords(std::string& out, const std::vector<std::uint32_t>& words) {
    PutU32(out, static_cast<std::uint32_t>(words.size()));
    out.append(reinterpret_cast<const char*>(words.data()), words.size() * sizeof(std::uint32_t));
}

} // namespace

std::filesystem::path CompiledMaterialShaderPath(const std::filesystem::path& materialPath) {
    std::filesystem::path path = materialPath;
    path.replace_extension(".hemshader");
    return path;
}

bool WriteCompiledMaterialShader(const std::filesystem::path& path, const CompiledMaterialShader& shader,
                                 std::string& outError) {
    std::string bytes(kMagic, sizeof(kMagic));
    PutU32(bytes, shader.interfaceVersion);
    bytes.append(reinterpret_cast<const char*>(&shader.sourceHash), sizeof(shader.sourceHash));
    PutU32(bytes, static_cast<std::uint32_t>(shader.textures.size()));
    for (const CompiledMaterialShader::Texture& texture : shader.textures) {
        PutU32(bytes, texture.linear ? 1u : 0u);
        PutU32(bytes, static_cast<std::uint32_t>(texture.path.size()));
        bytes += texture.path;
    }
    PutWords(bytes, shader.spirv);
    PutWords(bytes, shader.spirvRayTraced);

    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            outError = "Could not write " + temporary.string();
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        outError = "Could not replace " + path.string();
        return false;
    }
    return true;
}

bool ReadCompiledMaterialShader(const std::filesystem::path& path, CompiledMaterialShader& outShader,
                                std::string& outError) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        outError = "Missing " + path.string();
        return false;
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < sizeof(kMagic) || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) {
        outError = path.string() + " is not a compiled material shader";
        return false;
    }
    const std::vector<char> payload(bytes.begin() + sizeof(kMagic), bytes.end());
    Reader reader(payload);
    CompiledMaterialShader shader;
    std::uint32_t textureCount = 0;
    if (!reader.U32(shader.interfaceVersion)) {
        outError = path.string() + " is truncated";
        return false;
    }
    if (shader.interfaceVersion != kMaterialShaderInterfaceVersion) {
        outError = path.string() + " was compiled for another engine version";
        return false;
    }
    bool ok = reader.U64(shader.sourceHash) && reader.U32(textureCount) && textureCount <= kMaxTextures;
    for (std::uint32_t i = 0; ok && i < textureCount; ++i) {
        std::uint32_t flags = 0;
        CompiledMaterialShader::Texture& texture = shader.textures.emplace_back();
        ok = reader.U32(flags) && reader.String(texture.path);
        texture.linear = (flags & 1u) != 0;
    }
    ok = ok && reader.Words(shader.spirv) && reader.Words(shader.spirvRayTraced) && reader.AtEnd() &&
         !shader.spirv.empty();
    if (!ok) {
        outError = path.string() + " is malformed";
        return false;
    }
    outShader = std::move(shader);
    return true;
}

std::uint64_t HashMaterialShaderSource(const std::string& source) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : source) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    return hash;
}

} // namespace Hephaestus
