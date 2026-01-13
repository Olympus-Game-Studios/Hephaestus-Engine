#include "HtmlEditorUIHost.h"

#include "UltralightPlatformSetup.h"

#include "../../Scene/Scene.h"
#include "../../Scene/Entity.h"
#include "../../Scene/Components.h"

#include <imgui.h>

#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <stdlib.h>
#endif

namespace Hephaestus::HtmlUI {

static std::string JsonEscape(const std::string& s) {
  std::ostringstream out;
  for (char c : s) {
    switch (c) {
    case '\\': out << "\\\\"; break;
    case '"': out << "\\\""; break;
    case '\n': out << "\\n"; break;
    case '\r': out << "\\r"; break;
    case '\t': out << "\\t"; break;
    default: out << c; break;
    }
  }
  return out.str();
}

static std::filesystem::path GetExecutableDir() {
#if defined(_WIN32)
  // MSVC UCRT provides __p__pgmptr() which returns a pointer to the full path
  // of the running executable. This is more reliable than current_path() when
  // launched from an IDE.
  char** pgm = __p__pgmptr();
  if (pgm && *pgm && (*pgm)[0] != '\0') {
    return std::filesystem::path(*pgm).parent_path();
  }
#endif
  return std::filesystem::current_path();
}

HtmlEditorUIHost::HtmlEditorUIHost() : bridge_(1) {}

HtmlEditorUIHost::~HtmlEditorUIHost() = default;

void HtmlEditorUIHost::SetScene(Scene* scene) { scene_ = scene; }

void HtmlEditorUIHost::SetDetailsLockedFlag(bool* detailsLocked) { detailsLocked_ = detailsLocked; }

void HtmlEditorUIHost::SetAssetsRoot(std::filesystem::path assetsRoot) {
  assetsRoot_ = std::move(assetsRoot);
}

void HtmlEditorUIHost::SetAssetOpenCallback(AssetOpenCallback callback) {
  assetOpenCallback_ = std::move(callback);
}

void HtmlEditorUIHost::SetAssetCreateCallback(AssetOpenCallback callback) {
  assetCreateCallback_ = std::move(callback);
}

void HtmlEditorUIHost::Init(VulkanDevice* vkdev, VkDescriptorPool imguiDescriptorPool) {
  vkdev_ = vkdev;
  imguiDescriptorPool_ = imguiDescriptorPool;

  // Base directory for file:/// URL resolution.
  // Ultralight expects to load ICU/certs from "resources/..." relative to this base.
  // We copy Ultralight's SDK resources/ folder next to the executable at build time.
  std::filesystem::path exeDir = GetExecutableDir();
  std::filesystem::path base = exeDir;
  std::filesystem::path logPath = exeDir / "temp_out.txt";

  UltralightPlatformSetup::Init(base.string(), logPath.string());

  renderer_ = ultralight::Renderer::Create();

  // Register bridge handlers.
  bridge_.RegisterHandler("scene.list", [this](const std::string& payload) -> std::string {
    (void)payload;
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    Entity selected = scene_->GetSelectedEntity();
    uint32_t selectedId = selected ? static_cast<uint32_t>(static_cast<entt::entity>(selected)) : 0;

    std::ostringstream json;
    json << "{\"ok\":true,\"selectedId\":" << selectedId << ",\"entities\":[";

    bool first = true;
    auto view = scene_->GetEntitiesWith<TagComponent>();
    for (auto entityHandle : view) {
      Entity e(entityHandle, scene_);
      auto& tag = e.GetComponent<TagComponent>();

      const char* typeLabel = "ENTITY";
      if (e.HasComponent<EnvironmentComponent>()) {
        typeLabel = "ENV";
      } else if (e.HasComponent<LightComponent>()) {
        typeLabel = "LIGHT";
      } else if (e.HasComponent<MeshRendererComponent>()) {
        typeLabel = "MESH";
      }

      bool isSelected = e.HasComponent<SelectionComponent>() && e.GetComponent<SelectionComponent>().selected;

      if (!first) json << ",";
      first = false;
      json << "{\"id\":" << static_cast<uint32_t>(entityHandle)
           << ",\"name\":\"" << JsonEscape(tag.tag) << "\""
           << ",\"type\":\"" << typeLabel << "\""
           << ",\"selected\":" << (isSelected ? "true" : "false")
           << "}";
    }

    json << "]}";
    return json.str();
  });

  bridge_.RegisterHandler("scene.select", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    if (detailsLocked_ && *detailsLocked_) {
      return std::string("{\"ok\":false,\"error\":\"details_locked\"}");
    }

    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    if (!idOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_id\"}");
    }

    entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
    if (!scene_->GetRegistry().valid(handle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    }

    scene_->SetSelectedEntity(Entity(handle, scene_));
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("entity.get", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    Entity e;
    if (auto idOpt = EditorMessageBridge::ParseIntField(payload, "id")) {
      entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
      if (!scene_->GetRegistry().valid(handle)) {
        return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
      }
      e = Entity(handle, scene_);
    } else {
      e = scene_->GetSelectedEntity();
    }

    if (!e) {
      return std::string("{\"ok\":true,\"entity\":null}");
    }

    auto& tag = e.GetComponent<TagComponent>();
    auto& t = e.GetComponent<TransformComponent>();
    Vec3 rotDeg = t.GetRotationDegrees();

    const char* typeLabel = "ENTITY";
    if (e.HasComponent<EnvironmentComponent>()) {
      typeLabel = "ENV";
    } else if (e.HasComponent<LightComponent>()) {
      typeLabel = "LIGHT";
    } else if (e.HasComponent<MeshRendererComponent>()) {
      typeLabel = "MESH";
    }

    std::ostringstream json;
    json << "{\"ok\":true,\"entity\":{";
    json << "\"id\":" << static_cast<uint32_t>(static_cast<entt::entity>(e)) << ",";
    json << "\"name\":\"" << JsonEscape(tag.tag) << "\",";
    json << "\"type\":\"" << typeLabel << "\",";
    json << "\"px\":" << t.position.x << ",\"py\":" << t.position.y << ",\"pz\":" << t.position.z << ",";
    json << "\"rx\":" << rotDeg.x << ",\"ry\":" << rotDeg.y << ",\"rz\":" << rotDeg.z << ",";
    json << "\"sx\":" << t.scale.x << ",\"sy\":" << t.scale.y << ",\"sz\":" << t.scale.z;
    json << "}}";
    return json.str();
  });

  bridge_.RegisterHandler("entity.rename", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }
    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    auto nameOpt = EditorMessageBridge::ParseStringField(payload, "name");
    if (!idOpt || !nameOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_fields\"}");
    }
    entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
    if (!scene_->GetRegistry().valid(handle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    }
    Entity e(handle, scene_);
    e.GetComponent<TagComponent>().tag = *nameOpt;
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("entity.set_transform", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }
    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    if (!idOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_id\"}");
    }

    entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
    if (!scene_->GetRegistry().valid(handle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    }

    Entity e(handle, scene_);
    if (!e.HasComponent<TransformComponent>()) {
      return std::string("{\"ok\":false,\"error\":\"no_transform\"}");
    }

    auto& t = e.GetComponent<TransformComponent>();

    auto px = EditorMessageBridge::ParseFloatField(payload, "px");
    auto py = EditorMessageBridge::ParseFloatField(payload, "py");
    auto pz = EditorMessageBridge::ParseFloatField(payload, "pz");
    auto rx = EditorMessageBridge::ParseFloatField(payload, "rx");
    auto ry = EditorMessageBridge::ParseFloatField(payload, "ry");
    auto rz = EditorMessageBridge::ParseFloatField(payload, "rz");
    auto sx = EditorMessageBridge::ParseFloatField(payload, "sx");
    auto sy = EditorMessageBridge::ParseFloatField(payload, "sy");
    auto sz = EditorMessageBridge::ParseFloatField(payload, "sz");

    if (px && py && pz) {
      t.position = Vec3(*px, *py, *pz);
    }
    if (sx && sy && sz) {
      t.scale = Vec3(*sx, *sy, *sz);
    }
    if (rx && ry && rz) {
      t.SetRotationDegrees(Vec3(*rx, *ry, *rz));
    }

    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("assets.list", [this](const std::string& payload) -> std::string {
    if (assetsRoot_.empty()) {
      return std::string("{\"ok\":false,\"error\":\"no_assets_root\"}");
    }

    std::error_code ec;
    if (!std::filesystem::exists(assetsRoot_, ec) || ec) {
      return std::string("{\"ok\":false,\"error\":\"assets_root_missing\"}");
    }

    std::filesystem::path rel = ".";
    if (auto p = EditorMessageBridge::ParseStringField(payload, "path")) {
      rel = std::filesystem::path(*p);
    }

    std::filesystem::path dir = (assetsRoot_ / rel).lexically_normal();
    // Clamp: if path escapes root, reset to root.
    auto dirStr = dir.generic_string();
    auto rootStr = assetsRoot_.lexically_normal().generic_string();
    if (dirStr.size() < rootStr.size() || dirStr.compare(0, rootStr.size(), rootStr) != 0) {
      dir = assetsRoot_;
      rel = ".";
    }

    if (!std::filesystem::is_directory(dir, ec) || ec) {
      dir = assetsRoot_;
      rel = ".";
    }

    struct Entry { std::string name; bool isDir; std::string relPath; };
    std::vector<Entry> dirs;
    std::vector<Entry> files;

    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); ++it) {
      const auto& p = it->path();
      const bool isDir = it->is_directory(ec) && !ec;
      std::error_code relEc;
      auto rp = std::filesystem::relative(p, assetsRoot_, relEc);
      std::string rpStr = relEc ? p.filename().string() : rp.generic_string();
      Entry e{ p.filename().string(), isDir, rpStr };
      if (isDir) dirs.push_back(std::move(e)); else files.push_back(std::move(e));
    }

    auto sortByName = [](const Entry& a, const Entry& b) { return a.name < b.name; };
    std::sort(dirs.begin(), dirs.end(), sortByName);
    std::sort(files.begin(), files.end(), sortByName);

    std::ostringstream json;
    json << "{\"ok\":true,\"path\":\"" << JsonEscape(rel.generic_string()) << "\",\"entries\":[";

    bool first = true;
    auto emit = [&](const Entry& e) {
      if (!first) json << ",";
      first = false;
      json << "{\"name\":\"" << JsonEscape(e.name) << "\",";
      json << "\"dir\":" << (e.isDir ? "true" : "false") << ",";
      json << "\"rel\":\"" << JsonEscape(e.relPath) << "\"}";
    };

    for (const auto& d : dirs) emit(d);
    for (const auto& f : files) emit(f);
    json << "]}";
    return json.str();
  });

  bridge_.RegisterHandler("assets.select", [this](const std::string& payload) -> std::string {
    if (assetsRoot_.empty()) {
      return std::string("{\"ok\":false,\"error\":\"no_assets_root\"}");
    }

    auto p = EditorMessageBridge::ParseStringField(payload, "path");
    if (!p) {
      return std::string("{\"ok\":false,\"error\":\"missing_path\"}");
    }

    std::filesystem::path rel = std::filesystem::path(*p);
    std::filesystem::path abs = (assetsRoot_ / rel).lexically_normal();

    // Clamp: if path escapes root, reject.
    auto absStr = abs.generic_string();
    auto rootStr = assetsRoot_.lexically_normal().generic_string();
    if (absStr.size() < rootStr.size() || absStr.compare(0, rootStr.size(), rootStr) != 0) {
      return std::string("{\"ok\":false,\"error\":\"path_escape\"}");
    }

    lastSelectedAssetRel_ = rel.generic_string();
    bridge_.EmitEvent("assets.selected", std::string("{\"path\":\"") + JsonEscape(lastSelectedAssetRel_) + "\"}");

    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("assets.get_selected", [this](const std::string& payload) -> std::string {
    (void)payload;
    std::ostringstream json;
    json << "{\"ok\":true,\"path\":\"" << JsonEscape(lastSelectedAssetRel_) << "\"}";
    return json.str();
  });

  bridge_.RegisterHandler("assets.open", [this](const std::string& payload) -> std::string {
    if (assetsRoot_.empty()) {
      return std::string("{\"ok\":false,\"error\":\"no_assets_root\"}");
    }

    std::filesystem::path rel = ".";
    if (auto p = EditorMessageBridge::ParseStringField(payload, "path")) {
      rel = std::filesystem::path(*p);
    }

    std::filesystem::path assetPath = (assetsRoot_ / rel).lexically_normal();
    
    // Clamp: if path escapes root, reject
    auto pathStr = assetPath.generic_string();
    auto rootStr = assetsRoot_.lexically_normal().generic_string();
    if (pathStr.size() < rootStr.size() || pathStr.compare(0, rootStr.size(), rootStr) != 0) {
      return std::string("{\"ok\":false,\"error\":\"path_escape\"}");
    }

    // Check if file exists
    std::error_code ec;
    if (!std::filesystem::exists(assetPath, ec) || ec || std::filesystem::is_directory(assetPath, ec)) {
      return std::string("{\"ok\":false,\"error\":\"file_not_found\"}");
    }

    if (assetOpenCallback_) {
      assetOpenCallback_(assetPath);
    }

    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("assets.create", [this](const std::string& payload) -> std::string {
    if (assetsRoot_.empty()) {
      return std::string("{\"ok\":false,\"error\":\"no_assets_root\"}");
    }

    std::filesystem::path rel = ".";
    if (auto p = EditorMessageBridge::ParseStringField(payload, "path")) {
      rel = std::filesystem::path(*p);
    }

    std::filesystem::path assetPath = (assetsRoot_ / rel).lexically_normal();
    
    // Clamp: if path escapes root, reject
    auto pathStr = assetPath.generic_string();
    auto rootStr = assetsRoot_.lexically_normal().generic_string();
    if (pathStr.size() < rootStr.size() || pathStr.compare(0, rootStr.size(), rootStr) != 0) {
      return std::string("{\"ok\":false,\"error\":\"path_escape\"}");
    }

    if (assetCreateCallback_) {
      assetCreateCallback_(assetPath);
    }

    return std::string("{\"ok\":true}");
  });

  // Light properties handlers
  bridge_.RegisterHandler("light.get", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    Entity e;
    if (auto idOpt = EditorMessageBridge::ParseIntField(payload, "id")) {
      entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
      if (!scene_->GetRegistry().valid(handle)) {
        return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
      }
      e = Entity(handle, scene_);
    } else {
      e = scene_->GetSelectedEntity();
    }

    if (!e || !e.HasComponent<LightComponent>()) {
      return std::string("{\"ok\":true,\"light\":null}");
    }

    auto& light = e.GetComponent<LightComponent>();
    std::ostringstream json;
    json << "{\"ok\":true,\"light\":{";
    json << "\"type\":" << static_cast<int>(light.type) << ",";
    json << "\"r\":" << light.color.x << ",\"g\":" << light.color.y << ",\"b\":" << light.color.z << ",";
    json << "\"intensity\":" << light.intensity << ",";
    json << "\"range\":" << light.range << ",";
    json << "\"constant\":" << light.constant << ",";
    json << "\"linear\":" << light.linear << ",";
    json << "\"quadratic\":" << light.quadratic << ",";
    json << "\"innerConeAngle\":" << light.innerConeAngle << ",";
    json << "\"outerConeAngle\":" << light.outerConeAngle << ",";
    json << "\"dirX\":" << light.direction.x << ",\"dirY\":" << light.direction.y << ",\"dirZ\":" << light.direction.z << ",";
    json << "\"castShadows\":" << (light.castShadows ? "true" : "false") << ",";
    json << "\"shadowBias\":" << light.shadowBias << ",";
    json << "\"shadowStrength\":" << light.shadowStrength << ",";
    json << "\"shadowDistance\":" << light.shadowDistance << ",";
    json << "\"shadowExtent\":" << light.shadowExtent;
    json << "}}";
    return json.str();
  });

  bridge_.RegisterHandler("light.set", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    if (!idOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_id\"}");
    }

    entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
    if (!scene_->GetRegistry().valid(handle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    }

    Entity e(handle, scene_);
    if (!e.HasComponent<LightComponent>()) {
      return std::string("{\"ok\":false,\"error\":\"no_light_component\"}");
    }

    auto& light = e.GetComponent<LightComponent>();

    if (auto type = EditorMessageBridge::ParseIntField(payload, "type")) {
      light.type = static_cast<LightType>(*type);
    }

    auto r = EditorMessageBridge::ParseFloatField(payload, "r");
    auto g = EditorMessageBridge::ParseFloatField(payload, "g");
    auto b = EditorMessageBridge::ParseFloatField(payload, "b");
    if (r && g && b) {
      light.color = Vec3(*r, *g, *b);
    }

    if (auto intensity = EditorMessageBridge::ParseFloatField(payload, "intensity")) {
      light.intensity = *intensity;
    }
    if (auto range = EditorMessageBridge::ParseFloatField(payload, "range")) {
      light.range = *range;
    }
    if (auto constant = EditorMessageBridge::ParseFloatField(payload, "constant")) {
      light.constant = *constant;
    }
    if (auto linear = EditorMessageBridge::ParseFloatField(payload, "linear")) {
      light.linear = *linear;
    }
    if (auto quadratic = EditorMessageBridge::ParseFloatField(payload, "quadratic")) {
      light.quadratic = *quadratic;
    }
    if (auto inner = EditorMessageBridge::ParseFloatField(payload, "innerConeAngle")) {
      light.innerConeAngle = *inner;
    }
    if (auto outer = EditorMessageBridge::ParseFloatField(payload, "outerConeAngle")) {
      light.outerConeAngle = *outer;
    }

    auto dirX = EditorMessageBridge::ParseFloatField(payload, "dirX");
    auto dirY = EditorMessageBridge::ParseFloatField(payload, "dirY");
    auto dirZ = EditorMessageBridge::ParseFloatField(payload, "dirZ");
    if (dirX && dirY && dirZ) {
      light.direction = Vec3(*dirX, *dirY, *dirZ);
    }

    if (auto castShadows = EditorMessageBridge::ParseIntField(payload, "castShadows")) {
      light.castShadows = *castShadows != 0;
    }
    if (auto shadowBias = EditorMessageBridge::ParseFloatField(payload, "shadowBias")) {
      light.shadowBias = *shadowBias;
    }
    if (auto shadowStrength = EditorMessageBridge::ParseFloatField(payload, "shadowStrength")) {
      light.shadowStrength = *shadowStrength;
    }
    if (auto shadowDistance = EditorMessageBridge::ParseFloatField(payload, "shadowDistance")) {
      light.shadowDistance = *shadowDistance;
    }
    if (auto shadowExtent = EditorMessageBridge::ParseFloatField(payload, "shadowExtent")) {
      light.shadowExtent = *shadowExtent;
    }

    return std::string("{\"ok\":true}");
  });

  // Environment properties handlers
  bridge_.RegisterHandler("environment.get", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    Entity e;
    if (auto idOpt = EditorMessageBridge::ParseIntField(payload, "id")) {
      entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
      if (!scene_->GetRegistry().valid(handle)) {
        return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
      }
      e = Entity(handle, scene_);
    } else {
      e = scene_->GetSelectedEntity();
    }

    if (!e || !e.HasComponent<EnvironmentComponent>()) {
      return std::string("{\"ok\":true,\"environment\":null}");
    }

    auto& env = e.GetComponent<EnvironmentComponent>();
    std::ostringstream json;
    json << "{\"ok\":true,\"environment\":{";
    json << "\"type\":" << static_cast<int>(env.type) << ",";
    json << "\"r\":" << env.solidColor.x << ",\"g\":" << env.solidColor.y << ",\"b\":" << env.solidColor.z << ",";
    json << "\"hdriPath\":\"" << JsonEscape(env.hdriPath) << "\",";
    json << "\"hdriIntensity\":" << env.hdriIntensity << ",";
    json << "\"enableRenderDistance\":" << (env.enableRenderDistance ? "true" : "false") << ",";
    json << "\"renderDistance\":" << env.renderDistance << ",";
    json << "\"infiniteRenderDistance\":" << (env.infiniteRenderDistance ? "true" : "false");
    json << "}}";
    return json.str();
  });

  bridge_.RegisterHandler("environment.set", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    if (!idOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_id\"}");
    }

    entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
    if (!scene_->GetRegistry().valid(handle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    }

    Entity e(handle, scene_);
    if (!e.HasComponent<EnvironmentComponent>()) {
      return std::string("{\"ok\":false,\"error\":\"no_environment_component\"}");
    }

    auto& env = e.GetComponent<EnvironmentComponent>();

    if (auto type = EditorMessageBridge::ParseIntField(payload, "type")) {
      env.type = static_cast<EnvironmentComponent::BackgroundType>(*type);
    }

    auto r = EditorMessageBridge::ParseFloatField(payload, "r");
    auto g = EditorMessageBridge::ParseFloatField(payload, "g");
    auto b = EditorMessageBridge::ParseFloatField(payload, "b");
    if (r && g && b) {
      env.solidColor = Vec3(*r, *g, *b);
    }

    if (auto path = EditorMessageBridge::ParseStringField(payload, "hdriPath")) {
      env.hdriPath = *path;
    }
    if (auto intensity = EditorMessageBridge::ParseFloatField(payload, "hdriIntensity")) {
      env.hdriIntensity = *intensity;
    }
    if (auto enabled = EditorMessageBridge::ParseIntField(payload, "enableRenderDistance")) {
      env.enableRenderDistance = *enabled != 0;
    }
    if (auto distance = EditorMessageBridge::ParseFloatField(payload, "renderDistance")) {
      env.renderDistance = *distance;
    }
    if (auto infinite = EditorMessageBridge::ParseIntField(payload, "infiniteRenderDistance")) {
      env.infiniteRenderDistance = *infinite != 0;
    }

    return std::string("{\"ok\":true}");
  });

  // Mesh properties handlers
  bridge_.RegisterHandler("mesh.get", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    Entity e;
    if (auto idOpt = EditorMessageBridge::ParseIntField(payload, "id")) {
      entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
      if (!scene_->GetRegistry().valid(handle)) {
        return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
      }
      e = Entity(handle, scene_);
    } else {
      e = scene_->GetSelectedEntity();
    }

    if (!e || !e.HasComponent<MeshRendererComponent>()) {
      return std::string("{\"ok\":true,\"mesh\":null}");
    }

    auto& mesh = e.GetComponent<MeshRendererComponent>();
    const bool hasScript = e.HasComponent<LuaScriptComponent>();
    const std::string scriptPath = hasScript ? e.GetComponent<LuaScriptComponent>().scriptPath : std::string();
    std::ostringstream json;
    json << "{\"ok\":true,\"mesh\":{";
    json << "\"visible\":" << (mesh.visible ? "true" : "false") << ",";
    json << "\"backfaceCulling\":" << (mesh.backfaceCulling ? "true" : "false") << ",";
    json << "\"meshAssetPath\":\"" << JsonEscape(mesh.meshAssetPath) << "\",";
    json << "\"hasScript\":" << (hasScript ? "true" : "false") << ",";
    json << "\"scriptPath\":\"" << JsonEscape(scriptPath) << "\"";
    json << "}}";
    return json.str();
  });

  bridge_.RegisterHandler("mesh.set", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    if (!idOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_id\"}");
    }

    entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
    if (!scene_->GetRegistry().valid(handle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    }

    Entity e(handle, scene_);
    if (!e.HasComponent<MeshRendererComponent>()) {
      return std::string("{\"ok\":false,\"error\":\"no_mesh_component\"}");
    }

    auto& mesh = e.GetComponent<MeshRendererComponent>();

    if (auto visible = EditorMessageBridge::ParseIntField(payload, "visible")) {
      mesh.visible = *visible != 0;
    }
    if (auto culling = EditorMessageBridge::ParseIntField(payload, "backfaceCulling")) {
      mesh.backfaceCulling = *culling != 0;
    }

    return std::string("{\"ok\":true}");
  });

  // Lua script handler for meshes
  bridge_.RegisterHandler("mesh.assign_script", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    auto scriptPathOpt = EditorMessageBridge::ParseStringField(payload, "scriptPath");
    
    if (!idOpt || !scriptPathOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_fields\"}");
    }

    entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
    if (!scene_->GetRegistry().valid(handle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    }

    Entity e(handle, scene_);
    
    // Add or update LuaScriptComponent
    if (!e.HasComponent<LuaScriptComponent>()) {
      e.AddComponent<LuaScriptComponent>(*scriptPathOpt);
    } else {
      e.GetComponent<LuaScriptComponent>().scriptPath = *scriptPathOpt;
    }

    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("mesh.remove_script", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    if (!idOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_id\"}");
    }

    entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
    if (!scene_->GetRegistry().valid(handle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    }

    Entity e(handle, scene_);
    if (e.HasComponent<LuaScriptComponent>()) {
      e.RemoveComponent<LuaScriptComponent>();
    }

    return std::string("{\"ok\":true}");
  });

  outliner_ = std::make_unique<HtmlPanelView>("World Outliner", &bridge_, vkdev_, imguiDescriptorPool_);
  details_ = std::make_unique<HtmlPanelView>("Details", &bridge_, vkdev_, imguiDescriptorPool_);
  assetBrowser_ = std::make_unique<HtmlPanelView>("Asset Browser", &bridge_, vkdev_, imguiDescriptorPool_);
  logConsole_ = std::make_unique<HtmlPanelView>("Log Console", &bridge_, vkdev_, imguiDescriptorPool_);

  // Initial sizes will be overridden by ImGui window sizes.
  // These paths are resolved relative to the FileSystem base (exeDir).
  outliner_->Create(renderer_, 400, 600, "file:///editor/assets/ui/panels/outliner.html");
  details_->Create(renderer_, 400, 600, "file:///editor/assets/ui/panels/details.html");
  assetBrowser_->Create(renderer_, 800, 300, "file:///editor/assets/ui/panels/asset_browser.html");
  logConsole_->Create(renderer_, 800, 300, "file:///editor/assets/ui/panels/log_console.html");

  // Default JS event emitter targets log console view.
  bridge_.SetSendToJs([this](const std::string& jsCall) {
    // Broadcast to all panels (keep them in sync).
    if (outliner_) outliner_->EvaluateScript(jsCall);
    if (details_) details_->EvaluateScript(jsCall);
    if (assetBrowser_) assetBrowser_->EvaluateScript(jsCall);
    if (logConsole_) logConsole_->EvaluateScript(jsCall);
  });
}

void HtmlEditorUIHost::UpdateAndSync(float dt) {
  (void)dt;
  if (!renderer_)
    return;

  // Emit selection changed event.
  if (scene_) {
    Entity selected = scene_->GetSelectedEntity();
    uint32_t selectedId = selected ? static_cast<uint32_t>(static_cast<entt::entity>(selected)) : 0;
    if (selectedId != lastSelectedId_) {
      lastSelectedId_ = selectedId;
      std::ostringstream payload;
      payload << "{\"id\":" << selectedId << "}";
      bridge_.EmitEvent("selection.changed", payload.str());

      // Reset transform cache when selection changes.
      hasLastTransform_ = false;
      lastTransformId_ = selectedId;
    }

    // Emit transform updates for the selected entity (keeps Details in sync with gizmo).
    if (selectedId != 0 && selected.HasComponent<TransformComponent>()) {
      auto& t = selected.GetComponent<TransformComponent>();
      Vec3 rotDeg = t.GetRotationDegrees();

      float cur[9] = {
          t.position.x, t.position.y, t.position.z,
          rotDeg.x, rotDeg.y, rotDeg.z,
          t.scale.x, t.scale.y, t.scale.z
      };

      bool diff = !hasLastTransform_ || (lastTransformId_ != selectedId);
      if (!diff) {
        for (int i = 0; i < 9; ++i) {
          const float a = lastTransform_[i];
          const float b = cur[i];
          const float eps = 0.0001f;
          if (std::fabs(a - b) > eps) {
            diff = true;
            break;
          }
        }
      }

      if (diff) {
        lastTransformId_ = selectedId;
        for (int i = 0; i < 9; ++i) lastTransform_[i] = cur[i];
        hasLastTransform_ = true;

        std::ostringstream payload;
        payload << "{\"id\":" << selectedId
                << ",\"px\":" << cur[0] << ",\"py\":" << cur[1] << ",\"pz\":" << cur[2]
                << ",\"rx\":" << cur[3] << ",\"ry\":" << cur[4] << ",\"rz\":" << cur[5]
                << ",\"sx\":" << cur[6] << ",\"sy\":" << cur[7] << ",\"sz\":" << cur[8]
                << "}";
        bridge_.EmitEvent("entity.transform", payload.str());
      }
    }
  }

  renderer_->Update();
  renderer_->RefreshDisplay(0);
  renderer_->Render();

  // Sync all panels.
  if (outliner_) outliner_->SyncTextureToGpu();
  if (details_) details_->SyncTextureToGpu();
  if (assetBrowser_) assetBrowser_->SyncTextureToGpu();
  if (logConsole_) logConsole_->SyncTextureToGpu();
}

void HtmlEditorUIHost::DrawDockedPanels() {
  DrawOutlinerPanel();
  DrawDetailsPanel();
  DrawAssetBrowserPanel();
  DrawLogConsolePanel();
}

void HtmlEditorUIHost::DrawOutlinerPanel() {
  if (!outliner_)
    return;
  if (ImGui::Begin("World Outliner")) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    outliner_->Resize(static_cast<uint32_t>(std::max(1.0f, avail.x)),
                      static_cast<uint32_t>(std::max(1.0f, avail.y)));
    outliner_->DrawImGuiImage();
    outliner_->HandleImGuiInput();
  }
  ImGui::End();
}

void HtmlEditorUIHost::DrawDetailsPanel() {
  if (!details_)
    return;
  if (ImGui::Begin("Details")) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    details_->Resize(static_cast<uint32_t>(std::max(1.0f, avail.x)),
                     static_cast<uint32_t>(std::max(1.0f, avail.y)));
    details_->DrawImGuiImage();
    details_->HandleImGuiInput();
  }
  ImGui::End();
}

void HtmlEditorUIHost::DrawAssetBrowserPanel() {
  if (!assetBrowser_)
    return;
  if (ImGui::Begin("Asset Browser")) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    assetBrowser_->Resize(static_cast<uint32_t>(std::max(1.0f, avail.x)),
                          static_cast<uint32_t>(std::max(1.0f, avail.y)));
    assetBrowser_->DrawImGuiImage();
    assetBrowser_->HandleImGuiInput();
  }
  ImGui::End();
}

void HtmlEditorUIHost::DrawLogConsolePanel() {
  if (!logConsole_)
    return;
  if (ImGui::Begin("Log Console")) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    logConsole_->Resize(static_cast<uint32_t>(std::max(1.0f, avail.x)),
                        static_cast<uint32_t>(std::max(1.0f, avail.y)));
    logConsole_->DrawImGuiImage();
    logConsole_->HandleImGuiInput();
  }
  ImGui::End();
}

EditorMessageBridge& HtmlEditorUIHost::Bridge() { return bridge_; }

} // namespace Hephaestus::HtmlUI
