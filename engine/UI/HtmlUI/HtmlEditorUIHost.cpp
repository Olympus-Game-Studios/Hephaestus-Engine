#include "HtmlEditorUIHost.h"

#include "UltralightPlatformSetup.h"

#include "../../Scene/Scene.h"
#include "../../Scene/Entity.h"
#include "../../Scene/Components.h"
#include "../../Scripting/ScriptEngine.h"
#include "../../Scripting/ScriptRegistry.h"

#include <imgui.h>

#include "../MaterialEditor/MaterialGraph.h"

#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <system_error>
#include <vector>

#include "../EditorSpawnPreset.h"

#if defined(_WIN32)
#include <stdlib.h>
#endif

namespace MaterialEditor {
  bool LoadMaterialAssetFile(const std::filesystem::path& path,
                             MaterialComponent& outMaterial,
                             std::string& outDisplayName,
                             Graph& outGraph,
                             std::string& outError);

  bool EvaluateGraph(const Graph& graph,
                     MaterialComponent& outMaterial,
                     std::string& outErrorMessage);
} // namespace MaterialEditor

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

void HtmlEditorUIHost::SetScriptEngine(ScriptEngine* scriptEngine) {
  scriptEngine_ = scriptEngine;
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
      if (e.HasComponent<FolderComponent>()) {
        typeLabel = "FOLDER";
      } else if (e.HasComponent<EnvironmentComponent>()) {
        typeLabel = "ENV";
      } else if (e.HasComponent<LightComponent>()) {
        typeLabel = "LIGHT";
      } else if (e.HasComponent<MeshRendererComponent>()) {
        typeLabel = "MESH";
      }

      bool isSelected = e.HasComponent<SelectionComponent>() && e.GetComponent<SelectionComponent>().selected;
      uint32_t parentId = 0;
      if (e.HasComponent<TransformComponent>()) {
        auto& transform = e.GetComponent<TransformComponent>();
        if (transform.parent != entt::null) {
          parentId = static_cast<uint32_t>(transform.parent);
        }
      }

      if (!first) json << ",";
      first = false;
      json << "{\"id\":" << static_cast<uint32_t>(entityHandle)
           << ",\"name\":\"" << JsonEscape(tag.tag) << "\""
           << ",\"type\":\"" << typeLabel << "\""
           << ",\"parent\":" << parentId
           << ",\"isFolder\":" << (e.HasComponent<FolderComponent>() ? "true" : "false")
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

    const bool toggle = EditorMessageBridge::ParseIntField(payload, "toggle").value_or(0) != 0;
    const bool add = EditorMessageBridge::ParseIntField(payload, "add").value_or(0) != 0;

    if (toggle) {
      scene_->ToggleSelectedEntity(Entity(handle, scene_));
    } else if (add) {
      scene_->AddSelectedEntity(Entity(handle, scene_));
    } else {
      scene_->SetSelectedEntity(Entity(handle, scene_));
    }
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("scene.create_folder", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    std::string name = "Folder";
    if (auto nameOpt = EditorMessageBridge::ParseStringField(payload, "name")) {
      if (!nameOpt->empty()) {
        name = *nameOpt;
      }
    }

    Entity folder = scene_->CreateEntity(name);
    folder.AddComponent<FolderComponent>();

    if (auto parentOpt = EditorMessageBridge::ParseIntField(payload, "parent")) {
      entt::entity parentHandle = static_cast<entt::entity>(static_cast<uint32_t>(*parentOpt));
      if (scene_->GetRegistry().valid(parentHandle) && parentHandle != static_cast<entt::entity>(folder)) {
        scene_->SetParent(folder, Entity(parentHandle, scene_));
      }
    }

    std::ostringstream json;
    json << "{\"ok\":true,\"id\":" << static_cast<uint32_t>(static_cast<entt::entity>(folder)) << "}";
    return json.str();
  });

  bridge_.RegisterHandler("scene.get_selection", [this](const std::string& payload) -> std::string {
    (void)payload;
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    auto selected = scene_->GetSelectedEntities();
    Entity primary = scene_->GetSelectedEntity();
    uint32_t primaryId = primary ? static_cast<uint32_t>(static_cast<entt::entity>(primary)) : 0;

    std::ostringstream json;
    json << "{\"ok\":true,\"primaryId\":" << primaryId << ",\"ids\":[";
    bool first = true;
    for (auto& e : selected) {
      if (!e) continue;
      uint32_t id = static_cast<uint32_t>(static_cast<entt::entity>(e));
      if (!first) json << ",";
      first = false;
      json << id;
    }
    json << "]}";
    return json.str();
  });

  bridge_.RegisterHandler("scene.reparent", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    auto entityIdOpt = EditorMessageBridge::ParseIntField(payload, "entityId");
    auto newParentIdOpt = EditorMessageBridge::ParseIntField(payload, "newParentId");
    if (!entityIdOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_entityId\"}");
    }

    entt::entity entityHandle = static_cast<entt::entity>(static_cast<uint32_t>(*entityIdOpt));
    if (!scene_->GetRegistry().valid(entityHandle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    }

    Entity entity(entityHandle, scene_);

    // newParentId of 0 means unparent (move to root)
    uint32_t newParentId = newParentIdOpt.value_or(0);
    if (newParentId == 0) {
      // Unparent the entity
      scene_->RemoveParent(entity);
      return std::string("{\"ok\":true}");
    }

    entt::entity parentHandle = static_cast<entt::entity>(newParentId);
    if (!scene_->GetRegistry().valid(parentHandle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_parent\"}");
    }

    // Prevent parenting to itself
    if (entityHandle == parentHandle) {
      return std::string("{\"ok\":false,\"error\":\"cannot_parent_to_self\"}");
    }

    Entity parent(parentHandle, scene_);

    // Prevent circular hierarchy - check if parent is a descendant of entity
    Entity checkParent = parent;
    while (checkParent) {
      if (checkParent.HasComponent<TransformComponent>()) {
        auto& transform = checkParent.GetComponent<TransformComponent>();
        if (transform.parent == entityHandle) {
          return std::string("{\"ok\":false,\"error\":\"circular_hierarchy\"}");
        }
        if (transform.parent == entt::null) {
          break;
        }
        checkParent = Entity(transform.parent, scene_);
      } else {
        break;
      }
    }

    scene_->SetParent(entity, parent);
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("actor.spawn_preset", [this](const std::string& payload) -> std::string {
    auto presetOpt = EditorMessageBridge::ParseIntField(payload, "preset");
    if (!presetOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_preset\"}");
    }
    pendingAddActorsSpawn_ = static_cast<EditorSpawnPreset>(static_cast<uint32_t>(*presetOpt));
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("actor.drag_begin", [this](const std::string& payload) -> std::string {
    auto presetOpt = EditorMessageBridge::ParseIntField(payload, "preset");
    if (!presetOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_preset\"}");
    }
    pendingAddActorsDrag_ = static_cast<EditorSpawnPreset>(static_cast<uint32_t>(*presetOpt));
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("actor.drag_end", [this](const std::string& payload) -> std::string {
    (void)payload;
    pendingAddActorsDrag_.reset();
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("assets.drag_begin", [this](const std::string& payload) -> std::string {
    auto pathOpt = EditorMessageBridge::ParseStringField(payload, "path");
    if (!pathOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_path\"}");
    }
    pendingAssetDrag_ = *pathOpt;
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("assets.drag_end", [this](const std::string& payload) -> std::string {
    (void)payload;
    pendingAssetDrag_.reset();
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("outliner.drag_begin", [this](const std::string& payload) -> std::string {
    // Outliner entity drag is handled entirely in JS, this is just for logging/debugging
    (void)payload;
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("outliner.drag_end", [this](const std::string& payload) -> std::string {
    // Outliner entity drag is handled entirely in JS, this is just for logging/debugging
    (void)payload;
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("scene.outliner_drop_asset", [this](const std::string& payload) -> std::string {
    auto relOpt = EditorMessageBridge::ParseStringField(payload, "rel");
    auto folderOpt = EditorMessageBridge::ParseIntField(payload, "folderId");
    if (!relOpt || relOpt->empty() || !folderOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_fields\"}");
    }
    pendingOutlinerAssetDrop_ = std::make_pair(*relOpt, static_cast<uint32_t>(*folderOpt));
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
    if (e.HasComponent<FolderComponent>()) {
      typeLabel = "FOLDER";
    } else if (e.HasComponent<EnvironmentComponent>()) {
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

  bridge_.RegisterHandler("entity.set_transform_multi", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    auto idsStrOpt = EditorMessageBridge::ParseStringField(payload, "ids");
    if (!idsStrOpt || idsStrOpt->empty()) {
      return std::string("{\"ok\":false,\"error\":\"missing_ids\"}");
    }

    auto px = EditorMessageBridge::ParseFloatField(payload, "px");
    auto py = EditorMessageBridge::ParseFloatField(payload, "py");
    auto pz = EditorMessageBridge::ParseFloatField(payload, "pz");
    auto rx = EditorMessageBridge::ParseFloatField(payload, "rx");
    auto ry = EditorMessageBridge::ParseFloatField(payload, "ry");
    auto rz = EditorMessageBridge::ParseFloatField(payload, "rz");
    auto sx = EditorMessageBridge::ParseFloatField(payload, "sx");
    auto sy = EditorMessageBridge::ParseFloatField(payload, "sy");
    auto sz = EditorMessageBridge::ParseFloatField(payload, "sz");

    std::istringstream ss(*idsStrOpt);
    std::string token;
    int applied = 0;
    while (std::getline(ss, token, ',')) {
      if (token.empty()) continue;
      uint32_t id = static_cast<uint32_t>(std::strtoul(token.c_str(), nullptr, 10));
      if (!id) continue;
      entt::entity handle = static_cast<entt::entity>(id);
      if (!scene_->GetRegistry().valid(handle)) continue;

      Entity e(handle, scene_);
      if (!e.HasComponent<TransformComponent>()) continue;
      auto& t = e.GetComponent<TransformComponent>();

      if (px && py && pz) {
        t.position = Vec3(*px, *py, *pz);
      }
      if (sx && sy && sz) {
        t.scale = Vec3(*sx, *sy, *sz);
      }
      if (rx && ry && rz) {
        t.SetRotationDegrees(Vec3(*rx, *ry, *rz));
      }
      applied++;
    }

    std::ostringstream json;
    json << "{\"ok\":true,\"applied\":" << applied << "}";
    return json.str();
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

  bridge_.RegisterHandler("assets.list_meshes", [this](const std::string& payload) -> std::string {
    (void)payload;
    if (assetsRoot_.empty()) {
      return std::string("{\"ok\":false,\"error\":\"no_assets_root\"}");
    }

    std::filesystem::path root = assetsRoot_;
    std::filesystem::path meshesDir = assetsRoot_ / "meshes";
    std::error_code ec;
    if (std::filesystem::exists(meshesDir, ec) && !ec &&
        std::filesystem::is_directory(meshesDir, ec) && !ec) {
      root = meshesDir;
    }

    // Common mesh formats.
    auto isMeshExt = [](const std::string& ext) {
      std::string e;
      e.reserve(ext.size());
      for (char c : ext) e.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(c))));
      return (e == ".obj" || e == ".fbx" || e == ".gltf" || e == ".glb" || e == ".dae" || e == ".ply");
    };

    struct MeshEntry { std::string name; std::string rel; };
    std::vector<MeshEntry> out;
    out.reserve(128);

    const size_t kMax = 400;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); ++it) {
      const auto& p = it->path();
      if (it->is_directory(ec) && !ec) {
        continue;
      }
      if (!it->is_regular_file(ec) || ec) {
        ec.clear();
        continue;
      }

      if (!isMeshExt(p.extension().string())) {
        continue;
      }

      std::error_code relEc;
      auto rp = std::filesystem::relative(p, assetsRoot_, relEc);
      std::string relStr = relEc ? p.filename().string() : rp.generic_string();
      out.push_back(MeshEntry{ p.stem().string(), relStr });
      if (out.size() >= kMax) {
        break;
      }
    }

    std::sort(out.begin(), out.end(), [](const MeshEntry& a, const MeshEntry& b) {
      if (a.name == b.name) return a.rel < b.rel;
      return a.name < b.name;
    });

    std::ostringstream json;
    json << "{\"ok\":true,\"meshes\":[";
    bool first = true;
    for (const auto& m : out) {
      if (!first) json << ",";
      first = false;
      json << "{\"name\":\"" << JsonEscape(m.name) << "\",\"rel\":\"" << JsonEscape(m.rel) << "\"}";
    }
    json << "]}";
    return json.str();
  });

  bridge_.RegisterHandler("assets.list_materials", [this](const std::string& payload) -> std::string {
    (void)payload;
    if (assetsRoot_.empty()) {
      return std::string("{\"ok\":false,\"error\":\"no_assets_root\"}");
    }

    std::filesystem::path root = assetsRoot_;
    std::error_code ec;

    auto isMaterialExt = [](const std::string& ext) {
      std::string e;
      e.reserve(ext.size());
      for (char c : ext) e.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(c))));
      return (e == ".hematerial" || e == ".mat" || e == ".material");
    };

    struct MaterialEntry { std::string name; std::string rel; };
    std::vector<MaterialEntry> out;
    out.reserve(128);

    const size_t kMax = 400;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); ++it) {
      const auto& p = it->path();
      if (it->is_directory(ec) && !ec) {
        continue;
      }
      if (!it->is_regular_file(ec) || ec) {
        ec.clear();
        continue;
      }

      if (!isMaterialExt(p.extension().string())) {
        continue;
      }

      std::error_code relEc;
      auto rp = std::filesystem::relative(p, assetsRoot_, relEc);
      std::string relStr = relEc ? p.filename().string() : rp.generic_string();
      out.push_back(MaterialEntry{ p.stem().string(), relStr });
      if (out.size() >= kMax) {
        break;
      }
    }

    std::sort(out.begin(), out.end(), [](const MaterialEntry& a, const MaterialEntry& b) {
      if (a.name == b.name) return a.rel < b.rel;
      return a.name < b.name;
    });

    std::ostringstream json;
    json << "{\"ok\":true,\"materials\":[";
    bool first = true;
    for (const auto& m : out) {
      if (!first) json << ",";
      first = false;
      json << "{\"name\":\"" << JsonEscape(m.name) << "\",\"rel\":\"" << JsonEscape(m.rel) << "\"}";
    }
    json << "]}";
    return json.str();
  });

  bridge_.RegisterHandler("actor.spawn_mesh", [this](const std::string& payload) -> std::string {
    auto relOpt = EditorMessageBridge::ParseStringField(payload, "rel");
    if (!relOpt || relOpt->empty()) {
      return std::string("{\"ok\":false,\"error\":\"missing_rel\"}");
    }
    pendingAddActorsSpawnMeshRel_ = *relOpt;
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
    json << "\"hideInGame\":" << (mesh.hideInGame ? "true" : "false") << ",";
    json << "\"backfaceCulling\":" << (mesh.backfaceCulling ? "true" : "false") << ",";
    json << "\"meshAssetPath\":\"" << JsonEscape(mesh.meshAssetPath) << "\",";
    json << "\"materialIndices\":[";
    for (size_t i = 0; i < mesh.materialIndices.size(); ++i) {
      if (i) json << ",";
      json << mesh.materialIndices[i];
    }
    json << "],";
    json << "\"appliedMaterialPaths\":[";
    for (size_t i = 0; i < mesh.appliedMaterialPaths.size(); ++i) {
      if (i) json << ",";
      json << "\"" << JsonEscape(mesh.appliedMaterialPaths[i]) << "\"";
    }
    json << "],";
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
    if (auto hideInGame = EditorMessageBridge::ParseIntField(payload, "hideInGame")) {
      mesh.hideInGame = *hideInGame != 0;
    }
    if (auto culling = EditorMessageBridge::ParseIntField(payload, "backfaceCulling")) {
      mesh.backfaceCulling = *culling != 0;
    }
    if (auto materials = EditorMessageBridge::ParseIntArrayField(payload, "materialIndices")) {
      mesh.materialIndices = std::move(*materials);
    }

    return std::string("{\"ok\":true}");
  });

  // Material application handler
  bridge_.RegisterHandler("material.apply_to_mesh", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    auto materialPathOpt = EditorMessageBridge::ParseStringField(payload, "materialPath");
    auto slotOpt = EditorMessageBridge::ParseIntField(payload, "slot");
    
    if (!idOpt || !materialPathOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_fields\"}");
    }

    int slot = slotOpt ? *slotOpt : 0;
    if (slot < 0) {
      return std::string("{\"ok\":false,\"error\":\"invalid_slot\"}");
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

    // Ensure appliedMaterialPaths is large enough
    if (mesh.appliedMaterialPaths.size() <= static_cast<size_t>(slot)) {
      mesh.appliedMaterialPaths.resize(slot + 1, std::string());
    }

    // Empty materialPath means "clear"
    if (materialPathOpt->empty()) {
      mesh.appliedMaterialPaths[slot].clear();
      if (slot == 0 && e.HasComponent<MaterialComponent>()) {
        e.RemoveComponent<MaterialComponent>();
      }
      return std::string("{\"ok\":true}");
    }

    // Build absolute path to material
    std::filesystem::path materialPath = assetsRoot_ / *materialPathOpt;
    if (!std::filesystem::exists(materialPath)) {
      return std::string("{\"ok\":false,\"error\":\"material_file_not_found\"}");
    }

    // Store the material path for this slot
    mesh.appliedMaterialPaths[slot] = *materialPathOpt;
    
    // Also apply to the entity's MaterialComponent (for the first slot)
    if (slot == 0) {
      MaterialComponent loadedMaterial;
      std::string displayName;
      std::string loadError;
      MaterialEditor::Graph tempGraph;

      if (!MaterialEditor::LoadMaterialAssetFile(materialPath, loadedMaterial, displayName, tempGraph, loadError)) {
        return std::string("{\"ok\":false,\"error\":\"material_load_failed\"}");
      }

      // Evaluate the graph just like the Material Editor's Apply button does
      // This ensures WorldAlignedTexture nodes are properly handled
      std::string evalError;
      if (!MaterialEditor::EvaluateGraph(tempGraph, loadedMaterial, evalError)) {
        // If evaluation fails, still apply the loaded material (with warnings)
        std::cerr << "Graph evaluation failed for " << materialPath << ": " << evalError << std::endl;
      }

      if (e.HasComponent<MaterialComponent>()) {
        e.GetComponent<MaterialComponent>() = loadedMaterial;
      } else {
        e.AddComponent<MaterialComponent>(loadedMaterial);
      }
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

  // Mesh Collider handlers
  bridge_.RegisterHandler("mesh_collider.get", [this](const std::string& payload) -> std::string {
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
      return std::string("{\"ok\":true,\"collider\":null}");
    }

    if (!e.HasComponent<MeshColliderComponent>()) {
      return std::string("{\"ok\":true,\"collider\":null}");
    }

    auto& collider = e.GetComponent<MeshColliderComponent>();
    std::ostringstream json;
    json << "{\"ok\":true,\"collider\":{";
    json << "\"enabled\":" << (collider.enabled ? "true" : "false") << ",";
    json << "\"isStatic\":" << (collider.isStatic ? "true" : "false") << ",";
    json << "\"isTrigger\":" << (collider.isTrigger ? "true" : "false") << ",";
    json << "\"debugDraw\":" << (collider.debugDraw ? "true" : "false") << ",";
    json << "\"useComplexCollision\":" << (collider.useComplexCollision ? "true" : "false") << ",";
    json << "\"contacts\":[";
    bool first = true;
    for (const auto& contact : collider.contacts) {
      if (!first) json << ",";
      first = false;
      std::string otherName = "Entity";
      Entity other(contact.other, scene_);
      if (other && other.HasComponent<TagComponent>()) {
        otherName = other.GetComponent<TagComponent>().tag;
      } else if (!other) {
        otherName = "Entity (destroyed)";
      }
      json << "{\"other\":\"" << JsonEscape(otherName) << "\",";
      json << "\"pen\":" << contact.penetration << ",";
      json << "\"nx\":" << contact.normal.x << ",";
      json << "\"ny\":" << contact.normal.y << ",";
      json << "\"nz\":" << contact.normal.z << ",";
      json << "\"trigger\":" << (contact.trigger ? "true" : "false") << "}";
    }
    json << "]}}";
    return json.str();
  });

  bridge_.RegisterHandler("mesh_collider.add", [this](const std::string& payload) -> std::string {
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
    if (!e.HasComponent<MeshColliderComponent>()) {
      e.AddComponent<MeshColliderComponent>();
    }
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("mesh_collider.remove", [this](const std::string& payload) -> std::string {
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
    if (e.HasComponent<MeshColliderComponent>()) {
      e.RemoveComponent<MeshColliderComponent>();
    }
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("mesh_collider.set", [this](const std::string& payload) -> std::string {
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
    if (!e.HasComponent<MeshColliderComponent>()) {
      return std::string("{\"ok\":false,\"error\":\"no_mesh_collider\"}");
    }

    auto& collider = e.GetComponent<MeshColliderComponent>();
    if (auto v = EditorMessageBridge::ParseIntField(payload, "enabled")) collider.enabled = (*v != 0);
    if (auto v = EditorMessageBridge::ParseIntField(payload, "isStatic")) collider.isStatic = (*v != 0);
    if (auto v = EditorMessageBridge::ParseIntField(payload, "isTrigger")) collider.isTrigger = (*v != 0);
    if (auto v = EditorMessageBridge::ParseIntField(payload, "debugDraw")) collider.debugDraw = (*v != 0);
    if (auto v = EditorMessageBridge::ParseIntField(payload, "useComplexCollision")) collider.useComplexCollision = (*v != 0);
    return std::string("{\"ok\":true}");
  });

  // Physics Shape handlers
  bridge_.RegisterHandler("physics_shape.get", [this](const std::string& payload) -> std::string {
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

    if (!e || !e.HasComponent<PhysicsShapeComponent>()) {
      return std::string("{\"ok\":true,\"shape\":null}");
    }

    auto& shape = e.GetComponent<PhysicsShapeComponent>();
    std::ostringstream json;
    json << "{\"ok\":true,\"shape\":{";
    json << "\"type\":" << static_cast<int>(shape.type) << ",";
    json << "\"extX\":" << shape.extents.x << ",";
    json << "\"extY\":" << shape.extents.y << ",";
    json << "\"extZ\":" << shape.extents.z << ",";
    json << "\"radius\":" << shape.radius << ",";
    json << "\"height\":" << shape.height << ",";
    json << "\"offX\":" << shape.offset.x << ",";
    json << "\"offY\":" << shape.offset.y << ",";
    json << "\"offZ\":" << shape.offset.z;
    json << "}}";
    return json.str();
  });

  bridge_.RegisterHandler("physics_shape.add", [this](const std::string& payload) -> std::string {
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
    if (!e.HasComponent<PhysicsShapeComponent>()) {
      auto& shape = e.AddComponent<PhysicsShapeComponent>();
      shape = PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f));
    }
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("physics_shape.remove", [this](const std::string& payload) -> std::string {
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
    if (e.HasComponent<PhysicsShapeComponent>()) {
      e.RemoveComponent<PhysicsShapeComponent>();
    }
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("physics_shape.set", [this](const std::string& payload) -> std::string {
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
    if (!e.HasComponent<PhysicsShapeComponent>()) {
      return std::string("{\"ok\":false,\"error\":\"no_physics_shape\"}");
    }

    auto& shape = e.GetComponent<PhysicsShapeComponent>();
    if (auto t = EditorMessageBridge::ParseIntField(payload, "type")) {
      switch (*t) {
      case 1:
        shape.type = PhysicsShapeType::Sphere;
        break;
      case 2:
        shape.type = PhysicsShapeType::Capsule;
        break;
      default:
        shape.type = PhysicsShapeType::Box;
        break;
      }
    }

    if (auto v = EditorMessageBridge::ParseFloatField(payload, "extX")) shape.extents.x = std::max(0.01f, *v);
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "extY")) shape.extents.y = std::max(0.01f, *v);
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "extZ")) shape.extents.z = std::max(0.01f, *v);
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "radius")) shape.radius = std::max(0.01f, *v);
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "height")) shape.height = std::max(0.01f, *v);
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "offX")) shape.offset.x = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "offY")) shape.offset.y = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "offZ")) shape.offset.z = *v;

    return std::string("{\"ok\":true}");
  });

  // C++ Script handlers
  bridge_.RegisterHandler("scripts.get", [this](const std::string& payload) -> std::string {
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

    std::ostringstream json;
    json << "{\"ok\":true,\"attached\":[";
    bool first = true;
    if (e && e.HasComponent<ScriptComponent>()) {
      auto& sc = e.GetComponent<ScriptComponent>();
      for (const auto& name : sc.scriptNames) {
        if (!first) json << ",";
        first = false;
        json << "\"" << JsonEscape(name) << "\"";
      }
    }
    json << "]}";
    return json.str();
  });

  bridge_.RegisterHandler("scripts.registered", [this](const std::string& payload) -> std::string {
    (void)payload;
    auto list = ScriptRegistry::GetRegisteredScripts();
    std::ostringstream json;
    json << "{\"ok\":true,\"items\":[";
    bool first = true;
    for (const auto& name : list) {
      if (!first) json << ",";
      first = false;
      json << "\"" << JsonEscape(name) << "\"";
    }
    json << "]}";
    return json.str();
  });

  bridge_.RegisterHandler("scripts.attach", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }
    if (!scriptEngine_) {
      return std::string("{\"ok\":false,\"error\":\"no_script_engine\"}");
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

    const std::string scriptName = *nameOpt;
    if (!ScriptRegistry::IsRegistered(scriptName)) {
      return std::string("{\"ok\":false,\"error\":\"not_registered\"}");
    }

    Entity e(handle, scene_);
    ScriptPtr existing = nullptr;
    if (e.HasComponent<ScriptComponent>()) {
      auto& sc = e.GetComponent<ScriptComponent>();
      existing = sc.GetScriptByName(scriptName);
    }
    if (existing) {
      return std::string("{\"ok\":false,\"error\":\"already_attached\"}");
    }

    ScriptPtr newScript = ScriptRegistry::CreateScript(scriptName, e, scene_);
    if (!newScript) {
      return std::string("{\"ok\":false,\"error\":\"create_failed\"}");
    }
    scriptEngine_->AttachScript(e, newScript, scriptName);
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("scripts.detach", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }
    if (!scriptEngine_) {
      return std::string("{\"ok\":false,\"error\":\"no_script_engine\"}");
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
    scriptEngine_->DetachScript(e, *nameOpt);
    return std::string("{\"ok\":true}");
  });

  // Lua Script handlers
  bridge_.RegisterHandler("lua.get", [this](const std::string& payload) -> std::string {
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

    if (!e || !e.HasComponent<LuaScriptComponent>()) {
      return std::string("{\"ok\":true,\"lua\":null}");
    }

    auto& lua = e.GetComponent<LuaScriptComponent>();
    std::ostringstream json;
    json << "{\"ok\":true,\"lua\":{";
    json << "\"path\":\"" << JsonEscape(lua.scriptPath) << "\",";
    json << "\"enabled\":" << (lua.enabled ? "true" : "false");
    json << "}}";
    return json.str();
  });

  bridge_.RegisterHandler("lua.attach", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    auto pathOpt = EditorMessageBridge::ParseStringField(payload, "path");
    if (!idOpt || !pathOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_fields\"}");
    }

    entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
    if (!scene_->GetRegistry().valid(handle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    }

    Entity e(handle, scene_);
    if (!e.HasComponent<LuaScriptComponent>()) {
      e.AddComponent<LuaScriptComponent>();
    }

    auto& lua = e.GetComponent<LuaScriptComponent>();
    lua.scriptPath = *pathOpt;
    lua.enabled = true;
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("lua.remove", [this](const std::string& payload) -> std::string {
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
      auto& lua = e.GetComponent<LuaScriptComponent>();
      lua.scriptPath.clear();
      lua.enabled = true;
    }
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("lua.set_enabled", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    auto enabledOpt = EditorMessageBridge::ParseIntField(payload, "enabled");
    if (!idOpt || !enabledOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_fields\"}");
    }

    entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
    if (!scene_->GetRegistry().valid(handle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    }

    Entity e(handle, scene_);
    if (!e.HasComponent<LuaScriptComponent>()) {
      return std::string("{\"ok\":false,\"error\":\"no_lua_component\"}");
    }
    e.GetComponent<LuaScriptComponent>().enabled = (*enabledOpt != 0);
    return std::string("{\"ok\":true}");
  });

  // Rigid body (physics) handlers
  bridge_.RegisterHandler("rigidbody.get", [this](const std::string& payload) -> std::string {
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
      return std::string("{\"ok\":true,\"rigidbody\":null,\"hasPhysicsShape\":false}");
    }

    const bool hasShape = e.HasComponent<PhysicsShapeComponent>();
    if (!e.HasComponent<RigidBodyComponent>()) {
      std::ostringstream json;
      json << "{\"ok\":true,\"rigidbody\":null,\"hasPhysicsShape\":"
           << (hasShape ? "true" : "false") << "}";
      return json.str();
    }

    auto& rb = e.GetComponent<RigidBodyComponent>();
    std::ostringstream json;
    json << "{\"ok\":true,\"hasPhysicsShape\":" << (hasShape ? "true" : "false") << ",\"rigidbody\":{";
    json << "\"mass\":" << rb.mass << ",";
    json << "\"isStatic\":" << (rb.isStatic ? "true" : "false") << ",";
    json << "\"isTrigger\":" << (rb.isTrigger ? "true" : "false") << ",";
    json << "\"useGravity\":" << (rb.useGravity ? "true" : "false") << ",";
    json << "\"freezeRotation\":" << (rb.freezeRotation ? "true" : "false") << ",";
    json << "\"linearDamping\":" << rb.linearDamping << ",";
    json << "\"angularDamping\":" << rb.angularDamping << ",";
    json << "\"friction\":" << rb.friction << ",";
    json << "\"restitution\":" << rb.restitution << ",";
    json << "\"linVelX\":" << rb.linearVelocity.x << ",";
    json << "\"linVelY\":" << rb.linearVelocity.y << ",";
    json << "\"linVelZ\":" << rb.linearVelocity.z << ",";
    json << "\"angVelX\":" << rb.angularVelocity.x << ",";
    json << "\"angVelY\":" << rb.angularVelocity.y << ",";
    json << "\"angVelZ\":" << rb.angularVelocity.z << ",";
    json << "\"debugDraw\":" << (rb.debugDraw ? "true" : "false");
    json << "}}";
    return json.str();
  });

  bridge_.RegisterHandler("rigidbody.add", [this](const std::string& payload) -> std::string {
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
    if (!e.HasComponent<RigidBodyComponent>()) {
      auto& rb = e.AddComponent<RigidBodyComponent>();
      rb = RigidBodyComponent::CreateDynamic();
    }

    // Mirror legacy editor behavior: ensure a default shape exists.
    if (!e.HasComponent<PhysicsShapeComponent>()) {
      auto& shape = e.AddComponent<PhysicsShapeComponent>();
      shape = PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f));
    }

    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("rigidbody.remove", [this](const std::string& payload) -> std::string {
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
    if (e.HasComponent<RigidBodyComponent>()) {
      e.RemoveComponent<RigidBodyComponent>();
    }

    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("rigidbody.preset", [this](const std::string& payload) -> std::string {
    if (!scene_) {
      return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    }

    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    auto presetOpt = EditorMessageBridge::ParseStringField(payload, "preset");
    if (!idOpt || !presetOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_fields\"}");
    }

    entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(*idOpt));
    if (!scene_->GetRegistry().valid(handle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    }

    Entity e(handle, scene_);
    if (!e.HasComponent<RigidBodyComponent>()) {
      auto& rb = e.AddComponent<RigidBodyComponent>();
      rb = RigidBodyComponent::CreateDynamic();
    }
    if (!e.HasComponent<PhysicsShapeComponent>()) {
      auto& shape = e.AddComponent<PhysicsShapeComponent>();
      shape = PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f));
    }

    auto& rb = e.GetComponent<RigidBodyComponent>();
    const std::string preset = *presetOpt;
    if (preset == "player") {
      rb.isStatic = false;
      rb.isTrigger = false;
      rb.useGravity = true;
      rb.freezeRotation = true;
      rb.mass = 1.0f;
      rb.friction = 1.0f;
      rb.restitution = 0.0f;
      rb.linearDamping = 0.1f;
      rb.angularDamping = 0.5f;
    } else if (preset == "ground") {
      rb.isStatic = true;
      rb.isTrigger = false;
      rb.useGravity = false;
      rb.freezeRotation = false;
      rb.mass = 0.0f;
      rb.friction = 0.5f;
      rb.restitution = 0.0f;
      rb.linearDamping = 0.0f;
      rb.angularDamping = 0.0f;
    } else {
      return std::string("{\"ok\":false,\"error\":\"unknown_preset\"}");
    }

    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("rigidbody.set", [this](const std::string& payload) -> std::string {
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
    if (!e.HasComponent<RigidBodyComponent>()) {
      return std::string("{\"ok\":false,\"error\":\"no_rigidbody_component\"}");
    }

    auto& rb = e.GetComponent<RigidBodyComponent>();

    if (auto v = EditorMessageBridge::ParseFloatField(payload, "mass")) rb.mass = *v;
    if (auto v = EditorMessageBridge::ParseIntField(payload, "isStatic")) rb.isStatic = (*v != 0);
    if (auto v = EditorMessageBridge::ParseIntField(payload, "isTrigger")) rb.isTrigger = (*v != 0);
    if (auto v = EditorMessageBridge::ParseIntField(payload, "useGravity")) rb.useGravity = (*v != 0);
    if (auto v = EditorMessageBridge::ParseIntField(payload, "freezeRotation")) rb.freezeRotation = (*v != 0);
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "linearDamping")) rb.linearDamping = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "angularDamping")) rb.angularDamping = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "friction")) rb.friction = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "restitution")) rb.restitution = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "linVelX")) rb.linearVelocity.x = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "linVelY")) rb.linearVelocity.y = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "linVelZ")) rb.linearVelocity.z = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "angVelX")) rb.angularVelocity.x = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "angVelY")) rb.angularVelocity.y = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "angVelZ")) rb.angularVelocity.z = *v;
    if (auto v = EditorMessageBridge::ParseIntField(payload, "debugDraw")) rb.debugDraw = (*v != 0);

    // Match legacy editor constraints.
    if (rb.isStatic || rb.isTrigger) {
      rb.mass = 0.0f;
      rb.useGravity = false;
    } else {
      if (rb.mass <= 0.0f) rb.mass = 1.0f;
    }
    if (rb.linearDamping < 0.0f) rb.linearDamping = 0.0f;
    if (rb.angularDamping < 0.0f) rb.angularDamping = 0.0f;
    if (rb.friction < 0.0f) rb.friction = 0.0f;
    if (rb.restitution < 0.0f) rb.restitution = 0.0f;

    return std::string("{\"ok\":true}");
  });

  outliner_ = std::make_unique<HtmlPanelView>("World Outliner", &bridge_, vkdev_, imguiDescriptorPool_);
  addActors_ = std::make_unique<HtmlPanelView>("Add Actors", &bridge_, vkdev_, imguiDescriptorPool_);
  details_ = std::make_unique<HtmlPanelView>("Details", &bridge_, vkdev_, imguiDescriptorPool_);
  assetBrowser_ = std::make_unique<HtmlPanelView>("Asset Browser", &bridge_, vkdev_, imguiDescriptorPool_);
  logConsole_ = std::make_unique<HtmlPanelView>("Log Console", &bridge_, vkdev_, imguiDescriptorPool_);

  // Initial sizes will be overridden by ImGui window sizes.
  // These paths are resolved relative to the FileSystem base (exeDir).
  outliner_->Create(renderer_, 400, 600, "file:///editor/assets/ui/panels/outliner.html");
  addActors_->Create(renderer_, 520, 650, "file:///editor/assets/ui/panels/add_actors.html");
  details_->Create(renderer_, 400, 600, "file:///editor/assets/ui/panels/details.html");
  assetBrowser_->Create(renderer_, 800, 300, "file:///editor/assets/ui/panels/asset_browser.html");
  logConsole_->Create(renderer_, 800, 300, "file:///editor/assets/ui/panels/log_console.html");

  // Default JS event emitter targets log console view.
  bridge_.SetSendToJs([this](const std::string& jsCall) {
    // Broadcast to all panels (keep them in sync).
    if (outliner_) outliner_->EvaluateScript(jsCall);
    if (addActors_) addActors_->EvaluateScript(jsCall);
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
    auto selectedList = scene_->GetSelectedEntities();
    uint32_t selectionCount = static_cast<uint32_t>(selectedList.size());
    uint64_t selectionHash = 1469598103934665603ull;
    for (auto& e : selectedList) {
      if (!e) continue;
      uint64_t id = static_cast<uint64_t>(static_cast<uint32_t>(static_cast<entt::entity>(e)));
      selectionHash ^= id + 0x9e3779b97f4a7c15ull + (selectionHash << 6) + (selectionHash >> 2);
    }

    if (selectedId != lastSelectedId_ || selectionCount != lastSelectionCount_ || selectionHash != lastSelectionHash_) {
      lastSelectedId_ = selectedId;
      lastSelectionCount_ = selectionCount;
      lastSelectionHash_ = selectionHash;
      std::ostringstream payload;
      payload << "{\"id\":" << selectedId << ",\"count\":" << selectionCount << "}";
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
  // Only upload textures for panels that are actually visible. SyncTextureToGpu()
  // currently does CPU-side format conversion plus a synchronous GPU upload.
  if (outliner_ && outlinerVisible_) outliner_->SyncTextureToGpu();
  if (addActors_ && addActorsVisible_) addActors_->SyncTextureToGpu();
  if (details_ && detailsVisible_) details_->SyncTextureToGpu();
  if (assetBrowser_ && assetBrowserVisible_) assetBrowser_->SyncTextureToGpu();
  if (logConsole_ && logConsoleVisible_) logConsole_->SyncTextureToGpu();
}

void HtmlEditorUIHost::DrawDockedPanels() {
  DrawOutlinerPanel();
  DrawAddActorsPanel();
  DrawDetailsPanel();
  DrawAssetBrowserPanel();
  DrawLogConsolePanel();
}

void HtmlEditorUIHost::DrawOutlinerPanel() {
  if (!outliner_)
    return;
  outlinerVisible_ = false;
  if (ImGui::Begin("World Outliner")) {
    outlinerVisible_ = true;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    outliner_->Resize(static_cast<uint32_t>(std::max(1.0f, avail.x)),
                      static_cast<uint32_t>(std::max(1.0f, avail.y)));
    outliner_->DrawImGuiImage();
    outliner_->HandleImGuiInput();
  }
  ImGui::End();
}

void HtmlEditorUIHost::DrawAddActorsPanel() {
  if (!addActors_)
    return;
  addActorsVisible_ = false;
  if (ImGui::Begin("Add Actors")) {
    addActorsVisible_ = true;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    addActors_->Resize(static_cast<uint32_t>(std::max(1.0f, avail.x)),
                       static_cast<uint32_t>(std::max(1.0f, avail.y)));
    addActors_->DrawImGuiImage();

    // Allow HTML panel items to act as an ImGui drag source by piggy-backing on
    // the rendered image item.
    // JS signals the preset via actor.drag_begin/actor.drag_end.
    if (pendingAddActorsDrag_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        EditorSpawnPreset preset = *pendingAddActorsDrag_;
        ImGui::SetDragDropPayload("ACTOR_PALETTE_ITEM", &preset,
                                  sizeof(EditorSpawnPreset));
        ImGui::TextUnformatted("Add Actor");
        ImGui::EndDragDropSource();
      }
    }

    addActors_->HandleImGuiInput();
  }
  ImGui::End();
}

void HtmlEditorUIHost::DrawDetailsPanel() {
  if (!details_)
    return;
  detailsVisible_ = false;
  if (ImGui::Begin("Details")) {
    detailsVisible_ = true;
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
  assetBrowserVisible_ = false;
  if (ImGui::Begin("Asset Browser")) {
    assetBrowserVisible_ = true;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    assetBrowser_->Resize(static_cast<uint32_t>(std::max(1.0f, avail.x)),
                          static_cast<uint32_t>(std::max(1.0f, avail.y)));
    assetBrowser_->DrawImGuiImage();

    // Allow HTML panel items to act as an ImGui drag source by piggy-backing on
    // the rendered image item.
    // JS signals the asset path via assets.drag_begin/assets.drag_end.
    if (pendingAssetDrag_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        const std::string& assetPath = *pendingAssetDrag_;
        ImGui::SetDragDropPayload("ASSET_ITEM", assetPath.c_str(),
                                  assetPath.size() + 1);
        ImGui::TextUnformatted(assetPath.c_str());
        ImGui::EndDragDropSource();
      }
    }

    assetBrowser_->HandleImGuiInput();
  }
  ImGui::End();
}

void HtmlEditorUIHost::DrawLogConsolePanel() {
  if (!logConsole_)
    return;
  logConsoleVisible_ = false;
  if (ImGui::Begin("Log Console")) {
    logConsoleVisible_ = true;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    logConsole_->Resize(static_cast<uint32_t>(std::max(1.0f, avail.x)),
                        static_cast<uint32_t>(std::max(1.0f, avail.y)));
    logConsole_->DrawImGuiImage();
    logConsole_->HandleImGuiInput();
  }
  ImGui::End();
}

std::optional<EditorSpawnPreset> HtmlEditorUIHost::ConsumeAddActorsSpawnRequest() {
  auto v = pendingAddActorsSpawn_;
  pendingAddActorsSpawn_.reset();
  return v;
}

std::optional<std::string> HtmlEditorUIHost::ConsumeAddActorsSpawnMeshRequest() {
  auto v = pendingAddActorsSpawnMeshRel_;
  pendingAddActorsSpawnMeshRel_.reset();
  return v;
}

std::optional<std::pair<std::string, uint32_t>> HtmlEditorUIHost::ConsumeOutlinerAssetDropRequest() {
  auto v = pendingOutlinerAssetDrop_;
  pendingOutlinerAssetDrop_.reset();
  return v;
}

EditorMessageBridge& HtmlEditorUIHost::Bridge() { return bridge_; }

} // namespace Hephaestus::HtmlUI
