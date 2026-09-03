#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

enum class AssetType {
    Unknown = 0,
    Folder,
    Scene,
    Texture,
    StaticMesh,
    Material,
    Script,
    Audio,
    Prefab,
    Shader,
    Font,
    Text,
    UILayout,
    InputMap,
    Character,

    Blueprint = Prefab  // Legacy alias
};

struct AssetFingerprint {
    uint64_t sizeBytes = 0;
    uint64_t lastWriteTime = 0;
    std::string contentHash;
};

inline const char* ToString(AssetType type) {
    switch (type) {
        case AssetType::Folder: return "Folder";
        case AssetType::Scene: return "Scene";
        case AssetType::Texture: return "Texture";
        case AssetType::StaticMesh: return "StaticMesh";
        case AssetType::Material: return "Material";
        case AssetType::Script: return "Script";
        case AssetType::Audio: return "Audio";
        case AssetType::Prefab: return "Prefab";
        case AssetType::Shader: return "Shader";
        case AssetType::Font: return "Font";
        case AssetType::Text: return "Text";
        case AssetType::UILayout: return "UILayout";
        case AssetType::InputMap: return "InputMap";
        case AssetType::Character: return "Character";
        default: return "Unknown";
    }
}

inline AssetType AssetTypeFromString(const std::string& value) {
    if (value == "Folder") return AssetType::Folder;
    if (value == "Scene") return AssetType::Scene;
    if (value == "Texture") return AssetType::Texture;
    if (value == "StaticMesh") return AssetType::StaticMesh;
    if (value == "Material") return AssetType::Material;
    if (value == "Script") return AssetType::Script;
    if (value == "Audio") return AssetType::Audio;
    if (value == "Prefab") return AssetType::Prefab;
    if (value == "Blueprint") return AssetType::Prefab;
    if (value == "Shader") return AssetType::Shader;
    if (value == "Font") return AssetType::Font;
    if (value == "Text") return AssetType::Text;
    if (value == "UILayout") return AssetType::UILayout;
    if (value == "InputMap") return AssetType::InputMap;
    if (value == "Character") return AssetType::Character;
    return AssetType::Unknown;
}

/**
 * @brief Determine asset type from file extension.
 */
inline AssetType AssetTypeFromExtension(const std::string& extension) {
    static const std::unordered_map<std::string, AssetType> extensionMap = {
        // Scenes
        {".hpscene", AssetType::Scene},
        {".hescene", AssetType::Scene},
        {".scene", AssetType::Scene},
        
        // Meshes
        {".fbx", AssetType::StaticMesh},
        {".obj", AssetType::StaticMesh},
        {".gltf", AssetType::StaticMesh},
        {".glb", AssetType::StaticMesh},
        {".dae", AssetType::StaticMesh},
        
        // Textures
        {".png", AssetType::Texture},
        {".jpg", AssetType::Texture},
        {".jpeg", AssetType::Texture},
        {".tga", AssetType::Texture},
        {".dds", AssetType::Texture},
        {".hdr", AssetType::Texture},
        {".bmp", AssetType::Texture},
        {".exr", AssetType::Texture},
        
        // Materials
        {".hematerial", AssetType::Material},
        {".hpmat", AssetType::Material},
        {".material", AssetType::Material},
        {".mat", AssetType::Material},
        
        // Scripts
        {".lua", AssetType::Script},
        {".py", AssetType::Script},
        {".cs", AssetType::Script},
        {".cpp", AssetType::Script},
        {".h", AssetType::Script},
        {".hpp", AssetType::Script},
        
        // Audio
        {".wav", AssetType::Audio},
        {".ogg", AssetType::Audio},
        {".mp3", AssetType::Audio},
        {".flac", AssetType::Audio},
        
        // Characters
        {".hpcharacter", AssetType::Character},

        // Prefabs (and legacy blueprint extensions)
        {".hpprefab", AssetType::Prefab},
        {".hpblueprint", AssetType::Prefab},
        {".blueprint", AssetType::Prefab},
        
        // Shaders
        {".vert", AssetType::Shader},
        {".frag", AssetType::Shader},
        {".glsl", AssetType::Shader},
        {".hlsl", AssetType::Shader},
        {".spv", AssetType::Shader},
        
        // Fonts
        {".ttf", AssetType::Font},
        {".otf", AssetType::Font},
        
        // UI Layouts
        {".hui", AssetType::UILayout},
        {".hpinput", AssetType::InputMap},

        // Text
        {".txt", AssetType::Text},
        {".md", AssetType::Text},
        {".json", AssetType::Text},
        {".xml", AssetType::Text},
        {".yaml", AssetType::Text},
        {".yml", AssetType::Text},
        {".ini", AssetType::Text},
        {".cfg", AssetType::Text}
    };
    
    // Convert to lowercase for comparison
    std::string ext = extension;
    for (auto& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    
    auto it = extensionMap.find(ext);
    return (it != extensionMap.end()) ? it->second : AssetType::Unknown;
}
