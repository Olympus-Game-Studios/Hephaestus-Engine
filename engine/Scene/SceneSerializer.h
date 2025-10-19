#pragma once

#include <filesystem>
#include <string>

class Scene;

/**
 * SceneSerializer converts an in-memory Scene into a .hpscene document.
 * The current implementation writes a JSON-like ASCII document designed
 * for round-tripping editor data and keeping diffs human-readable.
 */
class SceneSerializer {
public:
    explicit SceneSerializer(const Scene& scene);

    bool Serialize(const std::filesystem::path& filepath) const;
    bool Serialize(const std::filesystem::path& filepath, std::string& outError) const;

    static bool Deserialize(Scene& scene, const std::filesystem::path& filepath);
    static bool Deserialize(Scene& scene, const std::filesystem::path& filepath, std::string& outError);

private:
    const Scene& m_Scene;
};
