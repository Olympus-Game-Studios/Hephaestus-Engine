#include "HtmlShellUIHost.h"

#include <Ultralight/Renderer.h>
#include <fstream>
#include <sstream>
#include <random>
#include <cctype>
#include <unordered_set>
#include <algorithm>
#include <system_error>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "UltralightPlatformSetup.h"

#include "../../Scene/Scene.h"
#include "../../Scene/Entity.h"
#include "../../Scene/Components.h"
#include "../../Scene/BlueprintAsset.h"
#include "../../Scene/BlueprintSerializer.h"
#include "../../Scene/BlueprintInstantiator.h"
#include "../../Assets/ModelImporter.h"
#include "../EditorSpawnPreset.h"

namespace Hephaestus::HtmlUI {

HtmlShellUIHost::HtmlShellUIHost() : bridge_(1) {}
HtmlShellUIHost::~HtmlShellUIHost() { DestroyPipeline(); }

static std::string SanitizeAssetName(const std::string& value) {
  std::string trimmed = value;
  while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) {
    trimmed.erase(trimmed.begin());
  }
  while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
    trimmed.pop_back();
  }
  if (trimmed.empty()) return std::string();

  std::string sanitized;
  sanitized.reserve(trimmed.size());

  char lastAppended = '\0';
  for (char ch : trimmed) {
    unsigned char uc = static_cast<unsigned char>(ch);
    if (std::isalnum(uc)) {
      sanitized.push_back(ch);
      lastAppended = ch;
    } else if (ch == '-' || ch == '_' || std::isspace(uc) || ch == '.') {
      if (lastAppended != '_') {
        sanitized.push_back('_');
        lastAppended = '_';
      }
    }
  }

  while (!sanitized.empty() && sanitized.back() == '_') {
    sanitized.pop_back();
  }

  return sanitized;
}

static std::string GenerateBlueprintId() {
  static const char kHexDigits[] = "0123456789abcdef";
  std::ostringstream out;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);
  for (int i = 0; i < 8; ++i) out << kHexDigits[dis(gen)];
  out << "-";
  for (int i = 0; i < 4; ++i) out << kHexDigits[dis(gen)];
  out << "-4";
  for (int i = 0; i < 3; ++i) out << kHexDigits[dis(gen)];
  out << "-";
  out << kHexDigits[(dis(gen) & 0x3) | 0x8];
  for (int i = 0; i < 3; ++i) out << kHexDigits[dis(gen)];
  out << "-";
  for (int i = 0; i < 12; ++i) out << kHexDigits[dis(gen)];
  return out.str();
}

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

static std::string JsonEscapeFull(const std::string& s) {
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

static JsonValue MakeNull() {
  JsonValue v;
  v.type = JsonValue::Type::Null;
  return v;
}

static JsonValue MakeString(const std::string& s) {
  JsonValue v;
  v.type = JsonValue::Type::String;
  v.stringValue = s;
  return v;
}

static JsonValue MakeNumber(double d) {
  JsonValue v;
  v.type = JsonValue::Type::Number;
  v.numberValue = d;
  return v;
}

static JsonValue MakeBool(bool value) {
  JsonValue v;
  v.type = JsonValue::Type::Bool;
  v.boolValue = value;
  return v;
}

static JsonValue MakeObject() {
  JsonValue v;
  v.type = JsonValue::Type::Object;
  return v;
}

static JsonValue MakeArray() {
  JsonValue v;
  v.type = JsonValue::Type::Array;
  return v;
}

static JsonValue MakeVec3Array(float x, float y, float z) {
  JsonValue v = MakeArray();
  v.arrayValue.push_back(MakeNumber(x));
  v.arrayValue.push_back(MakeNumber(y));
  v.arrayValue.push_back(MakeNumber(z));
  return v;
}

static void WriteJsonValuePretty(std::ostringstream& out, const JsonValue& value, int indent) {
  switch (value.type) {
    case JsonValue::Type::Null:
      out << "null";
      return;
    case JsonValue::Type::Bool:
      out << (value.boolValue ? "true" : "false");
      return;
    case JsonValue::Type::Number:
      out << value.numberValue;
      return;
    case JsonValue::Type::String:
      out << "\"" << JsonEscapeFull(value.stringValue) << "\"";
      return;
    case JsonValue::Type::Array: {
      out << "[";
      for (size_t i = 0; i < value.arrayValue.size(); ++i) {
        if (i) out << ", ";
        WriteJsonValuePretty(out, value.arrayValue[i], indent);
      }
      out << "]";
      return;
    }
    case JsonValue::Type::Object: {
      out << "{";
      bool first = true;
      for (const auto& [key, entry] : value.objectValue) {
        if (!first) out << ", ";
        out << "\"" << JsonEscapeFull(key) << "\": ";
        WriteJsonValuePretty(out, entry, indent + 2);
        first = false;
      }
      out << "}";
      return;
    }
  }
}

static void ExtractVec3(const JsonValue* value, float& x, float& y, float& z,
                        float dx = 0.0f, float dy = 0.0f, float dz = 0.0f) {
  x = dx; y = dy; z = dz;
  if (!value || !value->IsArray() || value->AsArray().size() < 3) return;
  const auto& arr = value->AsArray();
  if (arr[0].IsNumber()) x = static_cast<float>(arr[0].AsNumber());
  if (arr[1].IsNumber()) y = static_cast<float>(arr[1].AsNumber());
  if (arr[2].IsNumber()) z = static_cast<float>(arr[2].AsNumber());
}

static bool ParseJsonPayload(const std::string& payload, JsonValue& out, std::string& outError) {
  outError.clear();
  JsonParser parser(payload);
  if (!parser.Parse(out, outError)) {
    if (outError.empty()) outError = "Invalid json.";
    return false;
  }
  return true;
}

static JsonValue* EnsureObjectField(JsonValue& obj, const std::string& key) {
  if (!obj.IsObject()) {
    obj.type = JsonValue::Type::Object;
    obj.objectValue.clear();
  }
  auto it = obj.objectValue.find(key);
  if (it == obj.objectValue.end() || !it->second.IsObject()) {
    obj.objectValue[key] = MakeObject();
  }
  return &obj.objectValue[key];
}

static JsonValue* EnsureArrayField(JsonValue& obj, const std::string& key) {
  if (!obj.IsObject()) {
    obj.type = JsonValue::Type::Object;
    obj.objectValue.clear();
  }
  auto it = obj.objectValue.find(key);
  if (it == obj.objectValue.end() || !it->second.IsArray()) {
    obj.objectValue[key] = MakeArray();
  }
  return &obj.objectValue[key];
}

static JsonValue* EnsureComponentEntry(JsonValue& nodeObj, const std::string& componentType) {
  JsonValue* components = EnsureArrayField(nodeObj, "components");
  for (auto& entry : components->arrayValue) {
    if (!entry.IsObject()) continue;
    auto typeVal = entry.Find("type");
    if (typeVal && typeVal->IsString() && typeVal->AsString() == componentType) {
      return &entry;
    }
  }

  JsonValue newEntry = MakeObject();
  newEntry.objectValue["type"] = MakeString(componentType);
  newEntry.objectValue["enabled"] = MakeBool(true);
  newEntry.objectValue["data"] = MakeObject();
  components->arrayValue.push_back(std::move(newEntry));
  return &components->arrayValue.back();
}

static JsonValue* FindNodeById(JsonValue& root, const std::string& id) {
  if (!root.IsObject()) return nullptr;
  JsonValue* nodes = EnsureArrayField(root, "nodes");
  for (auto& nodeVal : nodes->arrayValue) {
    if (!nodeVal.IsObject()) continue;
    auto idVal = nodeVal.Find("id");
    if (idVal && idVal->IsString() && idVal->AsString() == id) {
      return &nodeVal;
    }
  }
  return nullptr;
}

static JsonValue* FindRootNode(JsonValue& root) {
  if (!root.IsObject()) return nullptr;
  const JsonValue* rootIdVal = root.Find("rootNodeId");
  std::string rootId = rootIdVal && rootIdVal->IsString() ? rootIdVal->AsString() : std::string();
  if (!rootId.empty()) return FindNodeById(root, rootId);

  JsonValue* nodes = EnsureArrayField(root, "nodes");
  for (auto& nodeVal : nodes->arrayValue) {
    if (!nodeVal.IsObject()) continue;
    auto parentVal = nodeVal.Find("parentId");
    if (!parentVal || parentVal->IsNull()) return &nodeVal;
  }
  return nullptr;
}

static bool ResolveBlueprintPath(const std::filesystem::path& requested,
                                 const std::filesystem::path& assetsRoot,
                                 std::filesystem::path& outPath,
                                 std::string& outError) {
  outError.clear();
  if (assetsRoot.empty()) {
    outError = "no_assets_root";
    return false;
  }

  std::filesystem::path rel = requested;
  if (rel.is_absolute()) {
    std::error_code ec;
    rel = std::filesystem::relative(rel, assetsRoot, ec);
    if (ec || rel.empty()) {
      outError = "path_escape";
      return false;
    }
  }

  std::filesystem::path abs = (assetsRoot / rel).lexically_normal();
  auto absStr = abs.generic_string();
  auto rootStr = assetsRoot.lexically_normal().generic_string();
  if (absStr.size() < rootStr.size() || absStr.compare(0, rootStr.size(), rootStr) != 0) {
    outError = "path_escape";
    return false;
  }

  outPath = abs;
  return true;
}

static std::string NormalizeBlueprintKey(const std::filesystem::path& absPath,
                                         const std::filesystem::path& assetsRoot) {
  if (assetsRoot.empty()) return absPath.lexically_normal().generic_string();
  std::error_code ec;
  auto rel = std::filesystem::relative(absPath, assetsRoot, ec);
  if (!ec && !rel.empty()) return rel.generic_string();
  return absPath.lexically_normal().generic_string();
}

static std::string ReadFileText(const std::filesystem::path& path, std::string& outError) {
  outError.clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    outError = "open_failed";
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static bool LoadBlueprintDocumentFromDisk(const std::filesystem::path& path,
                                          HtmlShellUIHost::BlueprintDocument& outDoc,
                                          std::string& outError) {
  outError.clear();
  std::string content = ReadFileText(path, outError);
  if (!outError.empty()) return false;

  JsonValue root;
  std::string parseError;
  JsonParser parser(content);
  if (!parser.Parse(root, parseError)) {
    outError = parseError.empty() ? "Invalid json." : parseError;
    return false;
  }
  if (!root.IsObject()) {
    outError = "Blueprint root must be an object.";
    return false;
  }

  outDoc.path = path;
  outDoc.root = std::move(root);
  outDoc.dirty = false;
  outDoc.error.clear();
  return true;
}

static bool SaveBlueprintDocumentToDisk(const HtmlShellUIHost::BlueprintDocument& doc,
                                        std::string& outError) {
  outError.clear();
  std::ofstream out(doc.path, std::ios::trunc);
  if (!out) {
    outError = "write_failed";
    return false;
  }
  std::ostringstream json;
  WriteJsonValuePretty(json, doc.root, 0);
  out << json.str();
  out.close();
  return true;
}

static const std::unordered_set<std::string> kKnownBlueprintComponents = {
  "MeshRenderer", "Camera", "ScriptComponent", "Light", "RigidBody",
  "MeshCollider", "PhysicsShape", "Material"
};

static std::string BuildBlueprintDocJson(const std::string& key, const JsonValue& root, bool dirty) {
  std::ostringstream json;
  json << "{\"path\":\"" << JsonEscape(key) << "\",";
  json << "\"dirty\":" << (dirty ? "true" : "false") << ",\"data\":{";

  std::string name;
  std::string guid;
  int assetVersion = 1;
  std::string rootNodeId;
  if (root.IsObject()) {
    if (const JsonValue* nameVal = root.Find("name")) {
      if (nameVal->IsString()) name = nameVal->AsString();
    }
    if (const JsonValue* guidVal = root.Find("guid")) {
      if (guidVal->IsString()) guid = guidVal->AsString();
    } else if (const JsonValue* idVal = root.Find("assetId")) {
      if (idVal->IsString()) guid = idVal->AsString();
    }
    if (const JsonValue* verVal = root.Find("assetVersion")) {
      if (verVal->IsNumber()) assetVersion = static_cast<int>(verVal->AsNumber());
    } else if (const JsonValue* legacyVer = root.Find("version")) {
      if (legacyVer->IsNumber()) assetVersion = static_cast<int>(legacyVer->AsNumber());
    }
    if (const JsonValue* rootVal = root.Find("rootNodeId")) {
      if (rootVal->IsString()) rootNodeId = rootVal->AsString();
    }
  }

  json << "\"name\":\"" << JsonEscape(name) << "\",";
  json << "\"guid\":\"" << JsonEscape(guid) << "\",";
  json << "\"assetVersion\":" << assetVersion << ",";
  json << "\"rootNodeId\":\"" << JsonEscape(rootNodeId) << "\",";
  json << "\"nodes\":[";

  const JsonValue* nodesVal = root.Find("nodes");
  bool firstNode = true;
  std::string scriptNodeId;
  std::string scriptSource;
  std::string scriptLanguage = "lua";
  if (nodesVal && nodesVal->IsArray()) {
    for (const auto& nodeVal : nodesVal->AsArray()) {
      if (!nodeVal.IsObject()) continue;
      const JsonValue* idVal = nodeVal.Find("id");
      std::string id = idVal && idVal->IsString() ? idVal->AsString() : std::string();
      if (id.empty()) continue;
      const JsonValue* nameVal = nodeVal.Find("name");
      std::string nodeName = nameVal && nameVal->IsString() ? nameVal->AsString() : id;
      const JsonValue* parentVal = nodeVal.Find("parentId");
      std::string parentId = (parentVal && parentVal->IsString()) ? parentVal->AsString() : std::string();

      float px, py, pz, rx, ry, rz, sx, sy, sz;
      px = py = pz = rx = ry = rz = 0.0f; sx = sy = sz = 1.0f;
      if (const JsonValue* transformVal = nodeVal.Find("transform")) {
        if (transformVal->IsObject()) {
          ExtractVec3(transformVal->Find("position"), px, py, pz, 0.0f, 0.0f, 0.0f);
          ExtractVec3(transformVal->Find("rotation"), rx, ry, rz, 0.0f, 0.0f, 0.0f);
          ExtractVec3(transformVal->Find("scale"), sx, sy, sz, 1.0f, 1.0f, 1.0f);
        }
      }

      if (!firstNode) json << ",";
      firstNode = false;
      json << "{\"id\":\"" << JsonEscape(id) << "\",";
      json << "\"name\":\"" << JsonEscape(nodeName) << "\",";
      if (parentId.empty()) {
        json << "\"parentId\":null,";
      } else {
        json << "\"parentId\":\"" << JsonEscape(parentId) << "\",";
      }
      json << "\"transform\":{\"position\":[" << px << "," << py << "," << pz << "],";
      json << "\"rotation\":[" << rx << "," << ry << "," << rz << "],";
      json << "\"scale\":[" << sx << "," << sy << "," << sz << "]},";

      json << "\"components\":[";
      bool firstComp = true;
      bool hasScript = false;
      const JsonValue* compsVal = nodeVal.Find("components");
      if (compsVal) {
        if (compsVal->IsArray()) {
          for (const auto& compEntry : compsVal->AsArray()) {
            if (!compEntry.IsObject()) continue;
            const JsonValue* typeVal = compEntry.Find("type");
            if (!typeVal || !typeVal->IsString()) continue;
            const std::string type = typeVal->AsString();
            const JsonValue* dataVal = compEntry.Find("data");
            if (!firstComp) json << ",";
            firstComp = false;
            bool known = kKnownBlueprintComponents.count(type) > 0;
            size_t fieldCount = (dataVal && dataVal->IsObject()) ? dataVal->AsObject().size() : 0;
            std::ostringstream compJson;
            if (dataVal) {
              WriteJsonValuePretty(compJson, *dataVal, 0);
            } else {
              compJson << "{}";
            }
            json << "{\"type\":\"" << JsonEscape(type) << "\",";
            json << "\"known\":" << (known ? "true" : "false") << ",";
            json << "\"fieldCount\":" << fieldCount << ",";
            json << "\"data\":\"" << JsonEscape(compJson.str()) << "\"}";

            if (type == "LuaScript") {
              hasScript = true;
              if (scriptNodeId.empty() && (parentVal == nullptr || parentId.empty())) {
                scriptNodeId = id;
                if (dataVal && dataVal->IsObject()) {
                  if (const JsonValue* langVal = dataVal->Find("language")) {
                    if (langVal->IsString()) scriptLanguage = langVal->AsString();
                  }
                  if (const JsonValue* srcVal = dataVal->Find("source")) {
                    if (srcVal->IsString()) scriptSource = srcVal->AsString();
                  }
                }
              }
            }
          }
        } else if (compsVal->IsObject()) {
          for (const auto& [key, entry] : compsVal->AsObject()) {
            if (!firstComp) json << ",";
            firstComp = false;
            bool known = kKnownBlueprintComponents.count(key) > 0;
            size_t fieldCount = entry.IsObject() ? entry.AsObject().size() : 0;
            std::ostringstream compJson;
            WriteJsonValuePretty(compJson, entry, 0);
            json << "{\"type\":\"" << JsonEscape(key) << "\",";
            json << "\"known\":" << (known ? "true" : "false") << ",";
            json << "\"fieldCount\":" << fieldCount << ",";
            json << "\"data\":\"" << JsonEscape(compJson.str()) << "\"}";
          }
        }
      }
      json << "],";

      if (const JsonValue* scriptVal = nodeVal.Find("script")) {
        if (scriptVal->IsObject()) {
          hasScript = true;
          if (scriptNodeId.empty() && (parentVal == nullptr || parentId.empty())) {
            scriptNodeId = id;
            if (const JsonValue* langVal = scriptVal->Find("language")) {
              if (langVal->IsString()) scriptLanguage = langVal->AsString();
            }
            if (const JsonValue* srcVal = scriptVal->Find("source")) {
              if (srcVal->IsString()) scriptSource = srcVal->AsString();
            }
          }
        }
      }

      json << "\"hasScript\":" << (hasScript ? "true" : "false");
      json << "}";
    }
  }

  json << "],";
  json << "\"scriptNodeId\":\"" << JsonEscape(scriptNodeId) << "\",";
  json << "\"scriptLanguage\":\"" << JsonEscape(scriptLanguage) << "\",";
  json << "\"scriptSource\":\"" << JsonEscape(scriptSource) << "\"";
  json << "}}";
  return json.str();
}

static std::filesystem::path GetExecutableDir() {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  std::filesystem::path path(buffer);
  return path.parent_path();
#else
  return std::filesystem::current_path();
#endif
}

void HtmlShellUIHost::Init(VulkanDevice* vkdev,
                           VkRenderPass renderPass,
                           VkExtent2D extent,
                           const std::filesystem::path& baseDir) {
  vkdev_ = vkdev;
  extent_ = extent;

  std::filesystem::path exeDir = baseDir.empty() ? GetExecutableDir() : baseDir;
  std::filesystem::path logPath = exeDir / "temp_out.txt";

  UltralightPlatformSetup::Init(exeDir.string(), logPath.string());
  renderer_ = ultralight::Renderer::Create();

  std::filesystem::path shellPath = exeDir / "editor" / "assets" / "ui" / "editor_shell.html";
  std::string url = std::string("file:///") + shellPath.generic_string();

  shell_ = std::make_unique<HtmlShellView>("EditorShell", &bridge_, vkdev_);
  shell_->Create(renderer_, extent.width, extent.height, url);

  bridge_.SetSendToJs([this](const std::string& jsCall) {
    if (shell_) {
      shell_->EvaluateScript(jsCall);
    }
  });

  auto jsonEscape = [](const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
      switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
      }
    }
    return out;
  };

  bridge_.RegisterHandler("viewport.set_rect", [this](const std::string& payload) -> std::string {
    auto x = EditorMessageBridge::ParseIntField(payload, "x");
    auto y = EditorMessageBridge::ParseIntField(payload, "y");
    auto w = EditorMessageBridge::ParseIntField(payload, "w");
    auto h = EditorMessageBridge::ParseIntField(payload, "h");
    if (x && y && w && h) {
      SetViewportRect(*x, *y, *w, *h);
      return std::string("{\"ok\":true}");
    }
    return std::string("{\"ok\":false,\"error\":\"missing_rect\"}");
  });

  bridge_.RegisterHandler("scene.list", [this, jsonEscape](const std::string& payload) -> std::string {
    (void)payload;
    if (!scene_) return std::string("{\"ok\":false,\"error\":\"no_scene\"}");

    Entity selected = scene_->GetSelectedEntity();
    uint32_t selectedId = selected ? static_cast<uint32_t>(static_cast<entt::entity>(selected)) : 0;

    std::ostringstream json;
    json << "{\"ok\":true,\"selectedId\":" << selectedId << ",\"entities\":[";
    bool first = true;
    auto view = scene_->GetEntitiesWith<TagComponent>();
    for (auto entityHandle : view) {
      Entity e(entityHandle, scene_);
      if (e.HasComponent<EditorOnlyComponent>()) {
        continue;
      }
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
           << ",\"name\":\"" << jsonEscape(tag.tag) << "\""
           << ",\"type\":\"" << typeLabel << "\""
           << ",\"parent\":" << parentId
           << ",\"isFolder\":" << (e.HasComponent<FolderComponent>() ? "true" : "false")
           << ",\"selected\":" << (isSelected ? "true" : "false")
           << "}";
    }
    json << "]}";
    bridge_.EmitEvent("sceneTreeUpdated", json.str());
    return json.str();
  });

  bridge_.RegisterHandler("scene.select", [this](const std::string& payload) -> std::string {
    if (!scene_) return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    if (!idOpt) return std::string("{\"ok\":false,\"error\":\"missing_id\"}");
    entt::entity ent = static_cast<entt::entity>(*idOpt);
    const bool toggle = EditorMessageBridge::ParseIntField(payload, "toggle").value_or(0) != 0;
    const bool add = EditorMessageBridge::ParseIntField(payload, "add").value_or(0) != 0;

    if (ent == entt::null || !scene_->GetRegistry().valid(ent)) {
      scene_->ClearSelection();
    } else {
      if (toggle) {
        scene_->ToggleSelectedEntity(Entity(ent, scene_));
      } else if (add) {
        scene_->AddSelectedEntity(Entity(ent, scene_));
      } else {
        scene_->SetSelectedEntity(Entity(ent, scene_));
      }
    }
    std::ostringstream evt;
    evt << "{\"entityId\":" << *idOpt << "}";
    bridge_.EmitEvent("selectionChanged", evt.str());
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("scene.get_selection", [this](const std::string& payload) -> std::string {
    (void)payload;
    if (!scene_) return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    std::ostringstream json;
    json << "{\"ok\":true,\"ids\":[";
    bool first = true;
    auto view = scene_->GetEntitiesWith<SelectionComponent>();
    uint32_t primaryId = 0;
    for (auto entityHandle : view) {
      Entity e(entityHandle, scene_);
      if (!e.GetComponent<SelectionComponent>().selected) continue;
      if (!first) json << ",";
      first = false;
      uint32_t id = static_cast<uint32_t>(entityHandle);
      if (primaryId == 0) primaryId = id;
      json << id;
    }
    json << "],\"primaryId\":" << primaryId << "}";
    return json.str();
  });

  bridge_.RegisterHandler("scene.reparent", [this](const std::string& payload) -> std::string {
    if (!scene_) return std::string("{\"ok\":false,\"error\":\"no_scene\"}");

    auto entityIdOpt = EditorMessageBridge::ParseIntField(payload, "entityId");
    auto newParentIdOpt = EditorMessageBridge::ParseIntField(payload, "newParentId");
    if (!entityIdOpt) return std::string("{\"ok\":false,\"error\":\"missing_entityId\"}");

    entt::entity entityHandle = static_cast<entt::entity>(static_cast<uint32_t>(*entityIdOpt));
    if (!scene_->GetRegistry().valid(entityHandle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    }

    Entity entity(entityHandle, scene_);

    uint32_t newParentId = newParentIdOpt.value_or(0);
    if (newParentId == 0) {
      scene_->RemoveParent(entity);
      return std::string("{\"ok\":true}");
    }

    entt::entity parentHandle = static_cast<entt::entity>(newParentId);
    if (!scene_->GetRegistry().valid(parentHandle)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_parent\"}");
    }

    if (entityHandle == parentHandle) {
      return std::string("{\"ok\":false,\"error\":\"cannot_parent_to_self\"}");
    }

    Entity parent(parentHandle, scene_);

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

  bridge_.RegisterHandler("scene.create_folder", [this](const std::string& payload) -> std::string {
    if (!scene_) return std::string("{\"ok\":false,\"error\":\"no_scene\"}");

    std::string name = "Folder";
    if (auto nameOpt = EditorMessageBridge::ParseStringField(payload, "name")) {
      if (!nameOpt->empty()) name = *nameOpt;
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

  bridge_.RegisterHandler("entity.create", [this](const std::string& payload) -> std::string {
    if (!scene_) return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    auto typeOpt = EditorMessageBridge::ParseStringField(payload, "type");
    std::string type = typeOpt ? *typeOpt : std::string("Empty");
    std::string name;
    if (auto nameOpt = EditorMessageBridge::ParseStringField(payload, "name")) {
      name = *nameOpt;
    }

    auto makeName = [&](const char* fallback) {
      if (!name.empty()) return name;
      return std::string(fallback);
    };

    Entity created = scene_->CreateEntity(makeName("Entity"));
    if (type == "Empty") {
      created.GetComponent<TagComponent>().tag = makeName("Empty Actor");
    } else if (type == "Camera") {
      created.GetComponent<TagComponent>().tag = makeName("Camera");
      created.AddComponent<CameraComponent>();
      auto& tr = created.GetComponent<TransformComponent>();
      tr.position = Vec3(0.0f, 1.5f, 3.0f);
    } else if (type == "LightDirectional") {
      created.GetComponent<TagComponent>().tag = makeName("Directional Light");
      created.AddComponent<LightComponent>(LightComponent::CreateDirectional(Vec3(0.0f, -1.0f, 0.0f)));
      auto& tr = created.GetComponent<TransformComponent>();
      tr.position = Vec3(0.0f, 3.0f, 0.0f);
      tr.rotation = Vec3(0.524f, 0.262f, 0.0f);
    } else if (type == "LightPoint") {
      created.GetComponent<TagComponent>().tag = makeName("Point Light");
      created.AddComponent<LightComponent>(LightComponent::CreatePoint(Vec3(1.0f, 1.0f, 1.0f), 5.0f, 10.0f));
      auto& tr = created.GetComponent<TransformComponent>();
      tr.position = Vec3(0.0f, 2.0f, 0.0f);
    } else if (type == "LightSpot") {
      created.GetComponent<TagComponent>().tag = makeName("Spot Light");
      created.AddComponent<LightComponent>(LightComponent::CreateSpot(Vec3(1.0f, 1.0f, 1.0f), 5.0f, 10.0f, 0.785f, 0.959f));
      auto& tr = created.GetComponent<TransformComponent>();
      tr.position = Vec3(0.0f, 3.0f, 0.0f);
      tr.rotation = Vec3(0.524f, 0.0f, 0.0f);
    } else if (type == "MeshCube") {
      created.GetComponent<TagComponent>().tag = makeName("Cube");
      auto& renderer = created.AddComponent<MeshRendererComponent>();
      renderer.meshAssetPath = "builtin://cube";
      created.AddComponent<MaterialComponent>(Vec3(1.0f, 1.0f, 1.0f));
    } else if (type == "MeshSphere") {
      created.GetComponent<TagComponent>().tag = makeName("Sphere");
      auto& renderer = created.AddComponent<MeshRendererComponent>();
      renderer.meshAssetPath = "builtin://sphere";
      created.AddComponent<MaterialComponent>(Vec3(1.0f, 1.0f, 1.0f));
    }

    if (auto parentOpt = EditorMessageBridge::ParseIntField(payload, "parentId")) {
      entt::entity parentHandle = static_cast<entt::entity>(static_cast<uint32_t>(*parentOpt));
      if (scene_->GetRegistry().valid(parentHandle) && parentHandle != static_cast<entt::entity>(created)) {
        scene_->SetParent(created, Entity(parentHandle, scene_));
      }
    }

    scene_->SetSelectedEntity(created);

    uint32_t createdId = static_cast<uint32_t>(static_cast<entt::entity>(created));
    std::ostringstream evt;
    evt << "{\"entityId\":" << createdId << "}";
    bridge_.EmitEvent("selectionChanged", evt.str());

    std::ostringstream json;
    json << "{\"ok\":true,\"id\":" << createdId << "}";
    return json.str();
  });

  bridge_.RegisterHandler("entity.get", [this, jsonEscape](const std::string& payload) -> std::string {
    if (!scene_) return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    if (!idOpt) return std::string("{\"ok\":false,\"error\":\"missing_id\"}");
    entt::entity ent = static_cast<entt::entity>(*idOpt);
    if (!scene_->GetRegistry().valid(ent)) {
      return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    }
    Entity e(ent, scene_);
    if (!e.HasComponent<TagComponent>() || !e.HasComponent<TransformComponent>()) {
      return std::string("{\"ok\":false,\"error\":\"missing_components\"}");
    }
    auto& tag = e.GetComponent<TagComponent>();
    auto& tr = e.GetComponent<TransformComponent>();
    Vec3 rotDeg = tr.GetRotationDegrees();
    const char* typeLabel = "ENTITY";
    if (e.HasComponent<EnvironmentComponent>()) typeLabel = "ENV";
    else if (e.HasComponent<LightComponent>()) typeLabel = "LIGHT";
    else if (e.HasComponent<MeshRendererComponent>()) typeLabel = "MESH";
    std::ostringstream json;
    json << "{\"ok\":true,\"entity\":{"
         << "\"id\":" << static_cast<uint32_t>(ent)
         << ",\"name\":\"" << jsonEscape(tag.tag) << "\""
         << ",\"type\":\"" << typeLabel << "\""
         << ",\"px\":" << tr.position.x
         << ",\"py\":" << tr.position.y
         << ",\"pz\":" << tr.position.z
            << ",\"rx\":" << rotDeg.x
            << ",\"ry\":" << rotDeg.y
            << ",\"rz\":" << rotDeg.z
         << ",\"sx\":" << tr.scale.x
         << ",\"sy\":" << tr.scale.y
         << ",\"sz\":" << tr.scale.z
         << "}}";
          bridge_.EmitEvent("entityDetailsUpdated", json.str());
    return json.str();
  });

  bridge_.RegisterHandler("entity.rename", [this](const std::string& payload) -> std::string {
    if (!scene_) return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    auto nameOpt = EditorMessageBridge::ParseStringField(payload, "name");
    if (!idOpt || !nameOpt) return std::string("{\"ok\":false,\"error\":\"missing_fields\"}");
    entt::entity ent = static_cast<entt::entity>(*idOpt);
    if (!scene_->GetRegistry().valid(ent)) return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    Entity e(ent, scene_);
    if (!e.HasComponent<TagComponent>()) return std::string("{\"ok\":false,\"error\":\"missing_tag\"}");
    e.GetComponent<TagComponent>().tag = *nameOpt;
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("entity.set_property", [this](const std::string& payload) -> std::string {
    if (!scene_) return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    auto pathOpt = EditorMessageBridge::ParseStringField(payload, "path");
    if (!idOpt || !pathOpt) return std::string("{\"ok\":false,\"error\":\"missing_fields\"}");
    entt::entity ent = static_cast<entt::entity>(*idOpt);
    if (!scene_->GetRegistry().valid(ent)) return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    Entity e(ent, scene_);
    const std::string& path = *pathOpt;
    if (path == "name") {
      auto valueOpt = EditorMessageBridge::ParseStringField(payload, "value");
      if (!valueOpt || !e.HasComponent<TagComponent>()) return std::string("{\"ok\":false,\"error\":\"invalid_value\"}");
      e.GetComponent<TagComponent>().tag = *valueOpt;
      return std::string("{\"ok\":true}");
    }
    if (!e.HasComponent<TransformComponent>()) return std::string("{\"ok\":false,\"error\":\"missing_transform\"}");
    auto& tr = e.GetComponent<TransformComponent>();
    auto vOpt = EditorMessageBridge::ParseFloatField(payload, "value");
    if (!vOpt) return std::string("{\"ok\":false,\"error\":\"invalid_value\"}");
    float v = *vOpt;
    if (path == "transform.px") tr.position.x = v;
    else if (path == "transform.py") tr.position.y = v;
    else if (path == "transform.pz") tr.position.z = v;
    else if (path == "transform.rx") {
      Vec3 deg = tr.GetRotationDegrees();
      tr.SetRotationDegrees(Vec3(v, deg.y, deg.z));
    }
    else if (path == "transform.ry") {
      Vec3 deg = tr.GetRotationDegrees();
      tr.SetRotationDegrees(Vec3(deg.x, v, deg.z));
    }
    else if (path == "transform.rz") {
      Vec3 deg = tr.GetRotationDegrees();
      tr.SetRotationDegrees(Vec3(deg.x, deg.y, v));
    }
    else if (path == "transform.sx") tr.scale.x = v;
    else if (path == "transform.sy") tr.scale.y = v;
    else if (path == "transform.sz") tr.scale.z = v;
    else return std::string("{\"ok\":false,\"error\":\"unknown_path\"}");
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("log", [this](const std::string& payload) -> std::string {
    (void)this;
    (void)payload;
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("entity.set_transform", [this](const std::string& payload) -> std::string {
    if (!scene_) return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    auto idOpt = EditorMessageBridge::ParseIntField(payload, "id");
    if (!idOpt) return std::string("{\"ok\":false,\"error\":\"missing_id\"}");
    entt::entity ent = static_cast<entt::entity>(*idOpt);
    if (!scene_->GetRegistry().valid(ent)) return std::string("{\"ok\":false,\"error\":\"invalid_entity\"}");
    Entity e(ent, scene_);
    if (!e.HasComponent<TransformComponent>()) return std::string("{\"ok\":false,\"error\":\"missing_transform\"}");
    auto& tr = e.GetComponent<TransformComponent>();
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "px")) tr.position.x = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "py")) tr.position.y = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "pz")) tr.position.z = *v;
    Vec3 deg = tr.GetRotationDegrees();
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "rx")) deg.x = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "ry")) deg.y = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "rz")) deg.z = *v;
    tr.SetRotationDegrees(deg);
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "sx")) tr.scale.x = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "sy")) tr.scale.y = *v;
    if (auto v = EditorMessageBridge::ParseFloatField(payload, "sz")) tr.scale.z = *v;
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("entity.set_transform_multi", [this](const std::string& payload) -> std::string {
    if (!scene_) return std::string("{\"ok\":false,\"error\":\"no_scene\"}");
    auto idsOpt = EditorMessageBridge::ParseIntArrayField(payload, "ids");
    if (!idsOpt) return std::string("{\"ok\":false,\"error\":\"missing_ids\"}");
    for (int id : *idsOpt) {
      entt::entity ent = static_cast<entt::entity>(id);
      if (!scene_->GetRegistry().valid(ent)) continue;
      Entity e(ent, scene_);
      if (!e.HasComponent<TransformComponent>()) continue;
      auto& tr = e.GetComponent<TransformComponent>();
      if (auto v = EditorMessageBridge::ParseFloatField(payload, "px")) tr.position.x = *v;
      if (auto v = EditorMessageBridge::ParseFloatField(payload, "py")) tr.position.y = *v;
      if (auto v = EditorMessageBridge::ParseFloatField(payload, "pz")) tr.position.z = *v;
      Vec3 deg = tr.GetRotationDegrees();
      if (auto v = EditorMessageBridge::ParseFloatField(payload, "rx")) deg.x = *v;
      if (auto v = EditorMessageBridge::ParseFloatField(payload, "ry")) deg.y = *v;
      if (auto v = EditorMessageBridge::ParseFloatField(payload, "rz")) deg.z = *v;
      tr.SetRotationDegrees(deg);
      if (auto v = EditorMessageBridge::ParseFloatField(payload, "sx")) tr.scale.x = *v;
      if (auto v = EditorMessageBridge::ParseFloatField(payload, "sy")) tr.scale.y = *v;
      if (auto v = EditorMessageBridge::ParseFloatField(payload, "sz")) tr.scale.z = *v;
    }
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("assets.list", [this, jsonEscape](const std::string& payload) -> std::string {
    auto pathOpt = EditorMessageBridge::ParseStringField(payload, "path");
    std::filesystem::path base = assetsRoot_.empty() ? std::filesystem::current_path() : assetsRoot_;
    std::filesystem::path path = pathOpt ? (base / *pathOpt) : base;
    std::error_code ec;
    std::filesystem::path rel = pathOpt ? std::filesystem::path(*pathOpt) : std::filesystem::path(".");
    std::string pathKey = path.generic_string();

    // Return cached result if available and path matches
    if (assetsListCacheValid_ && assetsListCachePath_ == pathKey) {
      return assetsListCacheJson_;
    }

    if (!std::filesystem::exists(path, ec)) {
      return std::string("{\"ok\":false,\"error\":\"path_not_found\"}");
    }

    std::ostringstream json;
    json << "{\"ok\":true,\"path\":\"" << jsonEscape(rel.generic_string()) << "\",\"entries\":[";
    bool first = true;
    for (auto& entry : std::filesystem::directory_iterator(path, ec)) {
      if (ec) break;
      std::string name = entry.path().filename().string();
      std::filesystem::path relPath = rel / entry.path().filename();
      if (!first) json << ",";
      first = false;
      json << "{\"name\":\"" << jsonEscape(name) << "\","
           << "\"rel\":\"" << jsonEscape(relPath.generic_string()) << "\","
           << "\"dir\":" << (entry.is_directory() ? "true" : "false") << "}";
    }
    json << "]}";

    // Cache the result
    assetsListCachePath_ = pathKey;
    assetsListCacheJson_ = json.str();
    assetsListCacheValid_ = true;

    bridge_.EmitEvent("assetsUpdated", assetsListCacheJson_);
    return assetsListCacheJson_;
  });

  bridge_.RegisterHandler("assets.select", [this, jsonEscape](const std::string& payload) -> std::string {
    if (assetsRoot_.empty()) return std::string("{\"ok\":false,\"error\":\"no_assets_root\"}");
    auto p = EditorMessageBridge::ParseStringField(payload, "path");
    if (!p) return std::string("{\"ok\":false,\"error\":\"missing_path\"}");

    std::filesystem::path rel = std::filesystem::path(*p);
    std::filesystem::path abs = (assetsRoot_ / rel).lexically_normal();
    auto absStr = abs.generic_string();
    auto rootStr = assetsRoot_.lexically_normal().generic_string();
    if (absStr.size() < rootStr.size() || absStr.compare(0, rootStr.size(), rootStr) != 0) {
      return std::string("{\"ok\":false,\"error\":\"path_escape\"}");
    }

    lastSelectedAssetRel_ = rel.generic_string();
    bridge_.EmitEvent("assets.selected", std::string("{\"path\":\"") + jsonEscape(lastSelectedAssetRel_) + "\"}");
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("assets.create", [this, jsonEscape](const std::string& payload) -> std::string {
    // Invalidate cache since we're about to create new files
    assetsListCacheValid_ = false;

    if (assetsRoot_.empty()) return std::string("{\"ok\":false,\"error\":\"no_assets_root\"}");

    auto kindOpt = EditorMessageBridge::ParseStringField(payload, "kind");
    if (!kindOpt || kindOpt->empty()) return std::string("{\"ok\":false,\"error\":\"missing_kind\"}");

    std::filesystem::path dirRel = ".";
    if (auto p = EditorMessageBridge::ParseStringField(payload, "dir")) {
      dirRel = std::filesystem::path(*p);
    }

    std::filesystem::path dirPath = (assetsRoot_ / dirRel).lexically_normal();
    auto dirStr = dirPath.generic_string();
    auto rootStr = assetsRoot_.lexically_normal().generic_string();
    if (dirStr.size() < rootStr.size() || dirStr.compare(0, rootStr.size(), rootStr) != 0) {
      return std::string("{\"ok\":false,\"error\":\"path_escape\"}");
    }

    auto nameOpt = EditorMessageBridge::ParseStringField(payload, "name");
    if (!nameOpt || nameOpt->empty()) return std::string("{\"ok\":false,\"error\":\"missing_name\"}");

    std::string sanitized = SanitizeAssetName(*nameOpt);
    if (sanitized.empty()) return std::string("{\"ok\":false,\"error\":\"invalid_name\"}");

    std::error_code createErr;
    std::filesystem::create_directories(dirPath, createErr);
    if (createErr) return std::string("{\"ok\":false,\"error\":\"create_directory_failed\"}");

    auto makeRel = [&](const std::filesystem::path& p) {
      std::error_code relEc;
      auto rp = std::filesystem::relative(p, assetsRoot_, relEc);
      return relEc ? p.filename().string() : rp.generic_string();
    };

    const std::string& kind = *kindOpt;
    if (kind == "folder") {
      std::filesystem::path folderPath = dirPath / sanitized;
      if (std::filesystem::exists(folderPath)) return std::string("{\"ok\":false,\"error\":\"already_exists\"}");
      std::filesystem::create_directories(folderPath, createErr);
      if (createErr) return std::string("{\"ok\":false,\"error\":\"create_folder_failed\"}");
      std::ostringstream json;
      json << "{\"ok\":true,\"path\":\"" << jsonEscape(makeRel(folderPath)) << "\"}";
      return json.str();
    }

    if (kind == "material") {
      std::filesystem::path materialPath = dirPath / (sanitized + ".hematerial");
      if (std::filesystem::exists(materialPath)) return std::string("{\"ok\":false,\"error\":\"already_exists\"}");
      std::ofstream out(materialPath, std::ios::trunc);
      if (!out) return std::string("{\"ok\":false,\"error\":\"write_failed\"}");
      out << "{\n  \"name\": \"" << sanitized << "\"\n}\n";
      out.close();
      std::ostringstream json;
      json << "{\"ok\":true,\"path\":\"" << jsonEscape(makeRel(materialPath)) << "\"}";
      return json.str();
    }

    if (kind == "lua_script") {
      std::filesystem::path scriptPath = dirPath / (sanitized + ".lua");
      if (std::filesystem::exists(scriptPath)) return std::string("{\"ok\":false,\"error\":\"already_exists\"}");
      std::ofstream out(scriptPath, std::ios::trunc);
      if (!out) return std::string("{\"ok\":false,\"error\":\"write_failed\"}");
      out << "-- " << sanitized << ".lua\n";
      out << "-- Lua script for Hephaestus Engine\n\n";
      out << "function onAwake()\n";
      out << "    print(\"[" << sanitized << "] Script awakened\")\n";
      out << "end\n\n";
      out << "function onUpdate(deltaTime)\n";
      out << "    -- Main game logic\n";
      out << "end\n\n";
      out << "function onDestroy()\n";
      out << "    print(\"[" << sanitized << "] Script destroyed\")\n";
      out << "end\n";
      out.close();
      std::ostringstream json;
      json << "{\"ok\":true,\"path\":\"" << jsonEscape(makeRel(scriptPath)) << "\"}";
      return json.str();
    }

    if (kind == "cpp_script") {
      std::filesystem::path headerPath = dirPath / (sanitized + ".h");
      std::filesystem::path implPath = dirPath / (sanitized + ".cpp");
      if (std::filesystem::exists(headerPath) || std::filesystem::exists(implPath)) {
        return std::string("{\"ok\":false,\"error\":\"already_exists\"}");
      }
      std::ofstream headerFile(headerPath, std::ios::trunc);
      if (!headerFile) return std::string("{\"ok\":false,\"error\":\"write_failed\"}");
      headerFile << "#pragma once\n";
      headerFile << "#include \"engine/Scripting/ScriptBase.h\"\n\n";
      headerFile << "class " << sanitized << " : public ScriptBase {\n";
      headerFile << "public:\n";
      headerFile << "    " << sanitized << "(Entity entity, Scene* scene) {\n";
      headerFile << "        m_Entity = entity;\n";
      headerFile << "        m_Scene = scene;\n";
      headerFile << "    }\n\n";
      headerFile << "    virtual ~" << sanitized << "() = default;\n\n";
      headerFile << "    void OnAwake() override {}\n";
      headerFile << "    void OnUpdate(float deltaTime) override {}\n";
      headerFile << "    void OnLateUpdate(float deltaTime) override {}\n";
      headerFile << "    void OnEnable() override {}\n";
      headerFile << "    void OnDisable() override {}\n";
      headerFile << "    void OnDestroy() override {}\n";
      headerFile << "};\n";
      headerFile.close();

      std::ofstream implFile(implPath, std::ios::trunc);
      if (!implFile) {
        std::filesystem::remove(headerPath);
        return std::string("{\"ok\":false,\"error\":\"write_failed\"}");
      }
      implFile << "#include \"" << sanitized << ".h\"\n";
      implFile.close();

      std::ostringstream json;
      json << "{\"ok\":true,\"path\":\"" << jsonEscape(makeRel(headerPath)) << "\"}";
      return json.str();
    }

    if (kind == "blueprint") {
      std::filesystem::path blueprintPath = dirPath / (sanitized + ".hpblueprint");
      if (std::filesystem::exists(blueprintPath)) return std::string("{\"ok\":false,\"error\":\"already_exists\"}");
      std::ofstream out(blueprintPath, std::ios::trunc);
      if (!out) return std::string("{\"ok\":false,\"error\":\"write_failed\"}");
      std::string blueprintId = GenerateBlueprintId();
      std::string rootNodeId = std::string("node_") + blueprintId;
      out << "{\n";
      out << "  \"schema\": \"HephaestusBlueprint\",\n";
      out << "  \"assetVersion\": 1,\n";
      out << "  \"guid\": \"" << blueprintId << "\",\n";
      out << "  \"name\": \"" << sanitized << "\",\n";
      out << "  \"rootNodeId\": \"" << rootNodeId << "\",\n";
      out << "  \"nodes\": [\n";
      out << "    {\n";
      out << "      \"id\": \"" << rootNodeId << "\",\n";
      out << "      \"name\": \"" << sanitized << "\",\n";
      out << "      \"parentId\": null,\n";
      out << "      \"transform\": {\n";
      out << "        \"position\": [0, 0, 0],\n";
      out << "        \"rotation\": [0, 0, 0],\n";
      out << "        \"scale\": [1, 1, 1]\n";
      out << "      },\n";
      out << "      \"components\": [],\n";
      out << "      \"exposed\": []\n";
      out << "    }\n";
      out << "  ]\n";
      out << "}\n";
      out.close();
      std::ostringstream json;
      json << "{\"ok\":true,\"path\":\"" << jsonEscape(makeRel(blueprintPath)) << "\"}";
      return json.str();
    }

    if (kind == "scene") {
      std::filesystem::path scenePath = dirPath / (sanitized + ".hpscene");
      if (std::filesystem::exists(scenePath)) return std::string("{\"ok\":false,\"error\":\"already_exists\"}");
      std::ofstream out(scenePath, std::ios::trunc);
      if (!out) return std::string("{\"ok\":false,\"error\":\"write_failed\"}");
      out << "{\n  \"name\": \"" << sanitized << "\",\n  \"entities\": []\n}\n";
      out.close();
      std::ostringstream json;
      json << "{\"ok\":true,\"path\":\"" << jsonEscape(makeRel(scenePath)) << "\"}";
      return json.str();
    }

    return std::string("{\"ok\":false,\"error\":\"unknown_kind\"}");
  });

  bridge_.RegisterHandler("assets.open", [this](const std::string& payload) -> std::string {
    (void)payload;
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("assets.list_meshes", [this, jsonEscape](const std::string& payload) -> std::string {
    (void)payload;
    std::filesystem::path base = assetsRoot_.empty() ? std::filesystem::current_path() : assetsRoot_;
    std::vector<std::pair<std::string, std::string>> meshes;
    for (auto& entry : std::filesystem::recursive_directory_iterator(base)) {
      if (!entry.is_regular_file()) continue;
      auto ext = entry.path().extension().string();
      std::string extLower;
      extLower.reserve(ext.size());
      for (char c : ext) extLower.push_back(static_cast<char>(std::tolower(c)));
      if (extLower == ".fbx" || extLower == ".obj" || extLower == ".gltf" || extLower == ".glb") {
        std::string rel = std::filesystem::relative(entry.path(), base).generic_string();
        meshes.emplace_back(entry.path().filename().string(), rel);
      }
    }
    std::ostringstream json;
    json << "{\"ok\":true,\"meshes\":[";
    bool first = true;
    for (auto& m : meshes) {
      if (!first) json << ",";
      first = false;
      json << "{\"name\":\"" << jsonEscape(m.first) << "\",\"rel\":\"" << jsonEscape(m.second) << "\"}";
    }
    json << "]}";
    return json.str();
  });

  bridge_.RegisterHandler("blueprint.load", [this](const std::string& payload) -> std::string {
    auto pathOpt = EditorMessageBridge::ParseStringField(payload, "path");
    if (!pathOpt) return std::string("{\"ok\":false,\"error\":\"missing_path\"}");

    std::filesystem::path absPath;
    std::string resolveError;
    if (!ResolveBlueprintPath(std::filesystem::path(*pathOpt), assetsRoot_, absPath, resolveError)) {
      return std::string("{\"ok\":false,\"error\":\"") + resolveError + "\"}";
    }

    std::string key = NormalizeBlueprintKey(absPath, assetsRoot_);
    auto it = blueprintDocs_.find(key);
    if (it == blueprintDocs_.end()) {
      BlueprintDocument doc;
      std::string loadError;
      if (!LoadBlueprintDocumentFromDisk(absPath, doc, loadError)) {
        return std::string("{\"ok\":false,\"error\":\"load_failed\",\"detail\":\"") + JsonEscape(loadError) + "\"}";
      }
      blueprintDocs_[key] = std::move(doc);
      blueprintTabOrder_.push_back(key);
      it = blueprintDocs_.find(key);
    }

    std::ostringstream json;
    json << "{\"ok\":true,\"doc\":" << BuildBlueprintDocJson(key, it->second.root, it->second.dirty) << "}";
    return json.str();
  });

  bridge_.RegisterHandler("blueprint.update_node", [this](const std::string& payload) -> std::string {
    JsonValue req;
    std::string parseError;
    if (!ParseJsonPayload(payload, req, parseError) || !req.IsObject()) {
      return std::string("{\"ok\":false,\"error\":\"bad_payload\"}");
    }

    const JsonValue* pathVal = req.Find("path");
    const JsonValue* nodeVal = req.Find("nodeId");
    const JsonValue* patchVal = req.Find("patch");
    if (!pathVal || !pathVal->IsString() || !nodeVal || !nodeVal->IsString() || !patchVal || !patchVal->IsObject()) {
      return std::string("{\"ok\":false,\"error\":\"missing_fields\"}");
    }

    std::filesystem::path absPath;
    std::string resolveError;
    if (!ResolveBlueprintPath(std::filesystem::path(pathVal->AsString()), assetsRoot_, absPath, resolveError)) {
      return std::string("{\"ok\":false,\"error\":\"") + resolveError + "\"}";
    }

    std::string key = NormalizeBlueprintKey(absPath, assetsRoot_);
    auto it = blueprintDocs_.find(key);
    if (it == blueprintDocs_.end()) {
      BlueprintDocument doc;
      std::string loadError;
      if (!LoadBlueprintDocumentFromDisk(absPath, doc, loadError)) {
        return std::string("{\"ok\":false,\"error\":\"load_failed\",\"detail\":\"") + JsonEscape(loadError) + "\"}";
      }
      blueprintDocs_[key] = std::move(doc);
      blueprintTabOrder_.push_back(key);
      it = blueprintDocs_.find(key);
    }

    JsonValue& root = it->second.root;
    JsonValue* nodeObj = FindNodeById(root, nodeVal->AsString());
    if (!nodeObj || !nodeObj->IsObject()) {
      return std::string("{\"ok\":false,\"error\":\"node_not_found\"}");
    }

    if (const JsonValue* nameVal = patchVal->Find("name")) {
      if (nameVal->IsString()) nodeObj->objectValue["name"] = MakeString(nameVal->AsString());
    }
    if (const JsonValue* parentVal = patchVal->Find("parentId")) {
      if (parentVal->IsNull()) nodeObj->objectValue["parentId"] = MakeNull();
      else if (parentVal->IsString()) nodeObj->objectValue["parentId"] = MakeString(parentVal->AsString());
    }
    if (const JsonValue* transformVal = patchVal->Find("transform")) {
      if (transformVal->IsObject()) {
        JsonValue* transformObj = EnsureObjectField(*nodeObj, "transform");
        float x, y, z;
        if (const JsonValue* posVal = transformVal->Find("position")) {
          ExtractVec3(posVal, x, y, z, 0.0f, 0.0f, 0.0f);
          transformObj->objectValue["position"] = MakeVec3Array(x, y, z);
        }
        if (const JsonValue* rotVal = transformVal->Find("rotation")) {
          ExtractVec3(rotVal, x, y, z, 0.0f, 0.0f, 0.0f);
          transformObj->objectValue["rotation"] = MakeVec3Array(x, y, z);
        }
        if (const JsonValue* scaleVal = transformVal->Find("scale")) {
          ExtractVec3(scaleVal, x, y, z, 1.0f, 1.0f, 1.0f);
          transformObj->objectValue["scale"] = MakeVec3Array(x, y, z);
        }
      }
    }

    it->second.dirty = true;
    std::ostringstream json;
    json << "{\"ok\":true,\"doc\":" << BuildBlueprintDocJson(key, it->second.root, it->second.dirty) << "}";
    return json.str();
  });

  bridge_.RegisterHandler("blueprint.update_script", [this](const std::string& payload) -> std::string {
    JsonValue req;
    std::string parseError;
    if (!ParseJsonPayload(payload, req, parseError) || !req.IsObject()) {
      return std::string("{\"ok\":false,\"error\":\"bad_payload\"}");
    }

    const JsonValue* pathVal = req.Find("path");
    const JsonValue* sourceVal = req.Find("source");
    const JsonValue* nodeVal = req.Find("nodeId");
    const JsonValue* langVal = req.Find("language");
    if (!pathVal || !pathVal->IsString() || !sourceVal || !sourceVal->IsString()) {
      return std::string("{\"ok\":false,\"error\":\"missing_fields\"}");
    }

    std::filesystem::path absPath;
    std::string resolveError;
    if (!ResolveBlueprintPath(std::filesystem::path(pathVal->AsString()), assetsRoot_, absPath, resolveError)) {
      return std::string("{\"ok\":false,\"error\":\"") + resolveError + "\"}";
    }

    std::string key = NormalizeBlueprintKey(absPath, assetsRoot_);
    auto it = blueprintDocs_.find(key);
    if (it == blueprintDocs_.end()) {
      BlueprintDocument doc;
      std::string loadError;
      if (!LoadBlueprintDocumentFromDisk(absPath, doc, loadError)) {
        return std::string("{\"ok\":false,\"error\":\"load_failed\",\"detail\":\"") + JsonEscape(loadError) + "\"}";
      }
      blueprintDocs_[key] = std::move(doc);
      blueprintTabOrder_.push_back(key);
      it = blueprintDocs_.find(key);
    }

    JsonValue* nodeObj = nullptr;
    if (nodeVal && nodeVal->IsString()) nodeObj = FindNodeById(it->second.root, nodeVal->AsString());
    if (!nodeObj) nodeObj = FindRootNode(it->second.root);
    if (!nodeObj || !nodeObj->IsObject()) {
      return std::string("{\"ok\":false,\"error\":\"node_not_found\"}");
    }

    JsonValue* scriptEntry = EnsureComponentEntry(*nodeObj, "LuaScript");
    JsonValue* scriptData = EnsureObjectField(*scriptEntry, "data");
    std::string language = (langVal && langVal->IsString()) ? langVal->AsString() : "lua";
    scriptData->objectValue["language"] = MakeString(language);
    scriptData->objectValue["source"] = MakeString(sourceVal->AsString());
    nodeObj->objectValue.erase("script");

    it->second.dirty = true;
    std::ostringstream json;
    json << "{\"ok\":true,\"doc\":" << BuildBlueprintDocJson(key, it->second.root, it->second.dirty) << "}";
    return json.str();
  });

  bridge_.RegisterHandler("blueprint.add_child", [this](const std::string& payload) -> std::string {
    JsonValue req;
    std::string parseError;
    if (!ParseJsonPayload(payload, req, parseError) || !req.IsObject()) {
      return std::string("{\"ok\":false,\"error\":\"bad_payload\"}");
    }

    const JsonValue* pathVal = req.Find("path");
    const JsonValue* parentVal = req.Find("parentId");
    if (!pathVal || !pathVal->IsString()) return std::string("{\"ok\":false,\"error\":\"missing_path\"}");

    std::filesystem::path absPath;
    std::string resolveError;
    if (!ResolveBlueprintPath(std::filesystem::path(pathVal->AsString()), assetsRoot_, absPath, resolveError)) {
      return std::string("{\"ok\":false,\"error\":\"") + resolveError + "\"}";
    }

    std::string key = NormalizeBlueprintKey(absPath, assetsRoot_);
    auto it = blueprintDocs_.find(key);
    if (it == blueprintDocs_.end()) {
      BlueprintDocument doc;
      std::string loadError;
      if (!LoadBlueprintDocumentFromDisk(absPath, doc, loadError)) {
        return std::string("{\"ok\":false,\"error\":\"load_failed\",\"detail\":\"") + JsonEscape(loadError) + "\"}";
      }
      blueprintDocs_[key] = std::move(doc);
      blueprintTabOrder_.push_back(key);
      it = blueprintDocs_.find(key);
    }

    JsonValue* nodes = EnsureArrayField(it->second.root, "nodes");
    std::string parentId = (parentVal && parentVal->IsString()) ? parentVal->AsString() : std::string();
    std::string newId = std::string("node_") + GenerateBlueprintId();

    JsonValue newNode = MakeObject();
    newNode.objectValue["id"] = MakeString(newId);
    newNode.objectValue["name"] = MakeString("NewNode");
    if (parentId.empty()) newNode.objectValue["parentId"] = MakeNull();
    else newNode.objectValue["parentId"] = MakeString(parentId);
    JsonValue transform = MakeObject();
    transform.objectValue["position"] = MakeVec3Array(0.0f, 0.0f, 0.0f);
    transform.objectValue["rotation"] = MakeVec3Array(0.0f, 0.0f, 0.0f);
    transform.objectValue["scale"] = MakeVec3Array(1.0f, 1.0f, 1.0f);
    newNode.objectValue["transform"] = std::move(transform);
    newNode.objectValue["components"] = MakeArray();
    newNode.objectValue["exposed"] = MakeArray();

    nodes->arrayValue.push_back(std::move(newNode));
    it->second.dirty = true;
    std::ostringstream json;
    json << "{\"ok\":true,\"doc\":" << BuildBlueprintDocJson(key, it->second.root, it->second.dirty) << "}";
    return json.str();
  });

  bridge_.RegisterHandler("blueprint.delete_node", [this](const std::string& payload) -> std::string {
    JsonValue req;
    std::string parseError;
    if (!ParseJsonPayload(payload, req, parseError) || !req.IsObject()) {
      return std::string("{\"ok\":false,\"error\":\"bad_payload\"}");
    }

    const JsonValue* pathVal = req.Find("path");
    const JsonValue* nodeVal = req.Find("nodeId");
    if (!pathVal || !pathVal->IsString() || !nodeVal || !nodeVal->IsString()) {
      return std::string("{\"ok\":false,\"error\":\"missing_fields\"}");
    }

    std::filesystem::path absPath;
    std::string resolveError;
    if (!ResolveBlueprintPath(std::filesystem::path(pathVal->AsString()), assetsRoot_, absPath, resolveError)) {
      return std::string("{\"ok\":false,\"error\":\"") + resolveError + "\"}";
    }

    std::string key = NormalizeBlueprintKey(absPath, assetsRoot_);
    auto it = blueprintDocs_.find(key);
    if (it == blueprintDocs_.end()) {
      BlueprintDocument doc;
      std::string loadError;
      if (!LoadBlueprintDocumentFromDisk(absPath, doc, loadError)) {
        return std::string("{\"ok\":false,\"error\":\"load_failed\",\"detail\":\"") + JsonEscape(loadError) + "\"}";
      }
      blueprintDocs_[key] = std::move(doc);
      blueprintTabOrder_.push_back(key);
      it = blueprintDocs_.find(key);
    }

    JsonValue* nodes = EnsureArrayField(it->second.root, "nodes");
    const std::string nodeId = nodeVal->AsString();

    JsonValue* nodeObj = FindNodeById(it->second.root, nodeId);
    if (!nodeObj) return std::string("{\"ok\":false,\"error\":\"node_not_found\"}");
    const JsonValue* parentVal = nodeObj->Find("parentId");
    if (!parentVal || parentVal->IsNull() || (parentVal->IsString() && parentVal->AsString().empty())) {
      return std::string("{\"ok\":false,\"error\":\"cannot_delete_root\"}");
    }

    for (auto& n : nodes->arrayValue) {
      if (!n.IsObject()) continue;
      const JsonValue* p = n.Find("parentId");
      if (p && p->IsString() && p->AsString() == nodeId) {
        n.objectValue["parentId"] = MakeNull();
      }
    }

    nodes->arrayValue.erase(
        std::remove_if(nodes->arrayValue.begin(), nodes->arrayValue.end(),
                       [&](const JsonValue& v) {
                         if (!v.IsObject()) return false;
                         const JsonValue* idVal = v.Find("id");
                         return idVal && idVal->IsString() && idVal->AsString() == nodeId;
                       }),
        nodes->arrayValue.end());

    it->second.dirty = true;
    std::ostringstream json;
    json << "{\"ok\":true,\"doc\":" << BuildBlueprintDocJson(key, it->second.root, it->second.dirty) << "}";
    return json.str();
  });

  bridge_.RegisterHandler("blueprint.add_component", [this](const std::string& payload) -> std::string {
    JsonValue req;
    std::string parseError;
    if (!ParseJsonPayload(payload, req, parseError) || !req.IsObject()) {
      return std::string("{\"ok\":false,\"error\":\"bad_payload\"}");
    }

    const JsonValue* pathVal = req.Find("path");
    const JsonValue* nodeVal = req.Find("nodeId");
    const JsonValue* compVal = req.Find("componentType");
    if (!pathVal || !pathVal->IsString() || !nodeVal || !nodeVal->IsString() || !compVal || !compVal->IsString()) {
      return std::string("{\"ok\":false,\"error\":\"missing_fields\"}");
    }

    std::filesystem::path absPath;
    std::string resolveError;
    if (!ResolveBlueprintPath(std::filesystem::path(pathVal->AsString()), assetsRoot_, absPath, resolveError)) {
      return std::string("{\"ok\":false,\"error\":\"") + resolveError + "\"}";
    }

    std::string key = NormalizeBlueprintKey(absPath, assetsRoot_);
    auto it = blueprintDocs_.find(key);
    if (it == blueprintDocs_.end()) {
      BlueprintDocument doc;
      std::string loadError;
      if (!LoadBlueprintDocumentFromDisk(absPath, doc, loadError)) {
        return std::string("{\"ok\":false,\"error\":\"load_failed\",\"detail\":\"") + JsonEscape(loadError) + "\"}";
      }
      blueprintDocs_[key] = std::move(doc);
      blueprintTabOrder_.push_back(key);
      it = blueprintDocs_.find(key);
    }

    JsonValue* nodeObj = FindNodeById(it->second.root, nodeVal->AsString());
    if (!nodeObj || !nodeObj->IsObject()) {
      return std::string("{\"ok\":false,\"error\":\"node_not_found\"}");
    }

    EnsureComponentEntry(*nodeObj, compVal->AsString());
    it->second.dirty = true;
    std::ostringstream json;
    json << "{\"ok\":true,\"doc\":" << BuildBlueprintDocJson(key, it->second.root, it->second.dirty) << "}";
    return json.str();
  });

  bridge_.RegisterHandler("blueprint.update_component", [this](const std::string& payload) -> std::string {
    JsonValue req;
    std::string parseError;
    if (!ParseJsonPayload(payload, req, parseError) || !req.IsObject()) {
      return std::string("{\"ok\":false,\"error\":\"bad_payload\"}");
    }

    const JsonValue* pathVal = req.Find("path");
    const JsonValue* nodeVal = req.Find("nodeId");
    const JsonValue* compVal = req.Find("componentType");
    const JsonValue* dataVal = req.Find("data");
    if (!pathVal || !pathVal->IsString() || !nodeVal || !nodeVal->IsString() || !compVal || !compVal->IsString() || !dataVal || !dataVal->IsString()) {
      return std::string("{\"ok\":false,\"error\":\"missing_fields\"}");
    }

    std::filesystem::path absPath;
    std::string resolveError;
    if (!ResolveBlueprintPath(std::filesystem::path(pathVal->AsString()), assetsRoot_, absPath, resolveError)) {
      return std::string("{\"ok\":false,\"error\":\"") + resolveError + "\"}";
    }

    std::string key = NormalizeBlueprintKey(absPath, assetsRoot_);
    auto it = blueprintDocs_.find(key);
    if (it == blueprintDocs_.end()) {
      BlueprintDocument doc;
      std::string loadError;
      if (!LoadBlueprintDocumentFromDisk(absPath, doc, loadError)) {
        return std::string("{\"ok\":false,\"error\":\"load_failed\",\"detail\":\"") + JsonEscape(loadError) + "\"}";
      }
      blueprintDocs_[key] = std::move(doc);
      blueprintTabOrder_.push_back(key);
      it = blueprintDocs_.find(key);
    }

    JsonValue* nodeObj = FindNodeById(it->second.root, nodeVal->AsString());
    if (!nodeObj || !nodeObj->IsObject()) {
      return std::string("{\"ok\":false,\"error\":\"node_not_found\"}");
    }

    JsonValue* compEntry = EnsureComponentEntry(*nodeObj, compVal->AsString());
    JsonValue parsed;
    std::string compError;
    if (!ParseJsonPayload(dataVal->AsString(), parsed, compError)) {
      return std::string("{\"ok\":false,\"error\":\"bad_component_json\",\"detail\":\"") + JsonEscape(compError) + "\"}";
    }
    compEntry->objectValue["data"] = std::move(parsed);

    it->second.dirty = true;
    std::ostringstream json;
    json << "{\"ok\":true,\"doc\":" << BuildBlueprintDocJson(key, it->second.root, it->second.dirty) << "}";
    return json.str();
  });

  bridge_.RegisterHandler("blueprint.save", [this](const std::string& payload) -> std::string {
    auto pathOpt = EditorMessageBridge::ParseStringField(payload, "path");
    if (!pathOpt) return std::string("{\"ok\":false,\"error\":\"missing_path\"}");

    std::filesystem::path absPath;
    std::string resolveError;
    if (!ResolveBlueprintPath(std::filesystem::path(*pathOpt), assetsRoot_, absPath, resolveError)) {
      return std::string("{\"ok\":false,\"error\":\"") + resolveError + "\"}";
    }

    std::string key = NormalizeBlueprintKey(absPath, assetsRoot_);
    auto it = blueprintDocs_.find(key);
    if (it == blueprintDocs_.end()) {
      return std::string("{\"ok\":false,\"error\":\"not_loaded\"}");
    }

    std::string saveError;
    if (!SaveBlueprintDocumentToDisk(it->second, saveError)) {
      return std::string("{\"ok\":false,\"error\":\"write_failed\",\"detail\":\"") + JsonEscape(saveError) + "\"}";
    }

    it->second.dirty = false;
    std::ostringstream json;
    json << "{\"ok\":true,\"doc\":" << BuildBlueprintDocJson(key, it->second.root, it->second.dirty) << "}";
    return json.str();
  });

  bridge_.RegisterHandler("actor.spawn_preset", [this](const std::string& payload) -> std::string {
    auto presetOpt = EditorMessageBridge::ParseIntField(payload, "preset");
    if (!presetOpt) return std::string("{\"ok\":false,\"error\":\"missing_preset\"}");
    if (!scene_) return std::string("{\"ok\":false,\"error\":\"no_scene\"}");

    std::string type = "Empty";
    switch (static_cast<EditorSpawnPreset>(static_cast<uint32_t>(*presetOpt))) {
      case EditorSpawnPreset::DefaultCube: type = "MeshCube"; break;
      case EditorSpawnPreset::DefaultSphere: type = "MeshSphere"; break;
      case EditorSpawnPreset::DirectionalLight: type = "LightDirectional"; break;
      case EditorSpawnPreset::PointLight: type = "LightPoint"; break;
      case EditorSpawnPreset::SpotLight: type = "LightSpot"; break;
      case EditorSpawnPreset::EmptyEntity: type = "Empty"; break;
      case EditorSpawnPreset::Camera: type = "Camera"; break;
      default: break;
    }

    std::string name;
    if (type == "Empty") name = "Empty Actor";
    else if (type == "Camera") name = "Camera";
    else if (type == "LightDirectional") name = "Directional Light";
    else if (type == "LightPoint") name = "Point Light";
    else if (type == "LightSpot") name = "Spot Light";
    else if (type == "MeshCube") name = "Cube";
    else if (type == "MeshSphere") name = "Sphere";
    else name = "Entity";

    Entity created = scene_->CreateEntity(name);
    if (type == "Camera") {
      created.AddComponent<CameraComponent>();
      auto& tr = created.GetComponent<TransformComponent>();
      tr.position = Vec3(0.0f, 1.5f, 3.0f);
    } else if (type == "LightDirectional") {
      created.AddComponent<LightComponent>(LightComponent::CreateDirectional(Vec3(0.0f, -1.0f, 0.0f)));
      auto& tr = created.GetComponent<TransformComponent>();
      tr.position = Vec3(0.0f, 3.0f, 0.0f);
      tr.rotation = Vec3(0.524f, 0.262f, 0.0f);
    } else if (type == "LightPoint") {
      created.AddComponent<LightComponent>(LightComponent::CreatePoint(Vec3(1.0f, 1.0f, 1.0f), 5.0f, 10.0f));
      auto& tr = created.GetComponent<TransformComponent>();
      tr.position = Vec3(0.0f, 2.0f, 0.0f);
    } else if (type == "LightSpot") {
      created.AddComponent<LightComponent>(LightComponent::CreateSpot(Vec3(1.0f, 1.0f, 1.0f), 5.0f, 10.0f, 0.785f, 0.959f));
      auto& tr = created.GetComponent<TransformComponent>();
      tr.position = Vec3(0.0f, 3.0f, 0.0f);
      tr.rotation = Vec3(0.524f, 0.0f, 0.0f);
    } else if (type == "MeshCube") {
      auto& renderer = created.AddComponent<MeshRendererComponent>();
      renderer.meshAssetPath = "builtin://cube";
      created.AddComponent<MaterialComponent>(Vec3(1.0f, 1.0f, 1.0f));
    } else if (type == "MeshSphere") {
      auto& renderer = created.AddComponent<MeshRendererComponent>();
      renderer.meshAssetPath = "builtin://sphere";
      created.AddComponent<MaterialComponent>(Vec3(1.0f, 1.0f, 1.0f));
    }

    scene_->SetSelectedEntity(created);
    uint32_t createdId = static_cast<uint32_t>(static_cast<entt::entity>(created));
    std::ostringstream json;
    json << "{\"ok\":true,\"id\":" << createdId << "}";
    return json.str();
  });

  bridge_.RegisterHandler("actor.spawn_mesh", [this](const std::string& payload) -> std::string {
    auto relOpt = EditorMessageBridge::ParseStringField(payload, "rel");
    if (!relOpt || relOpt->empty()) return std::string("{\"ok\":false,\"error\":\"missing_rel\"}");
    if (!scene_) return std::string("{\"ok\":false,\"error\":\"no_scene\"}");

    std::filesystem::path relPath = *relOpt;
    std::string name = relPath.stem().string();
    if (name.empty()) name = "Mesh";

    Entity created = scene_->CreateEntity(name);
    auto& renderer = created.AddComponent<MeshRendererComponent>();
    renderer.meshAssetPath = relPath.generic_string();
    created.AddComponent<MaterialComponent>(Vec3(1.0f, 1.0f, 1.0f));

    scene_->SetSelectedEntity(created);
    uint32_t createdId = static_cast<uint32_t>(static_cast<entt::entity>(created));
    std::ostringstream json;
    json << "{\"ok\":true,\"id\":" << createdId << "}";
    return json.str();
  });

  bridge_.RegisterHandler("blueprint.open", [this, jsonEscape](const std::string& payload) -> std::string {
    auto pathOpt = EditorMessageBridge::ParseStringField(payload, "path");
    if (!pathOpt) return std::string("{\"ok\":false,\"error\":\"missing_path\"}");
    std::ostringstream msg;
    msg << "{\"path\":\"" << jsonEscape(*pathOpt) << "\"}";
    bridge_.EmitEvent("blueprint.open", msg.str());
    return std::string("{\"ok\":true}");
  });

  CreatePipeline(renderPass, extent);
}

void HtmlShellUIHost::OnSwapchainRecreated(VkRenderPass renderPass, VkExtent2D extent) {
  extent_ = extent;
  DestroyPipeline();
  CreatePipeline(renderPass, extent);
  if (shell_) {
    shell_->Resize(extent.width, extent.height);
  }
}

void HtmlShellUIHost::UpdateAndSync(float dt) {
  (void)dt;
  if (!shell_) {
    std::cout << "[HtmlShellUIHost] UpdateAndSync: shell_ is null" << std::endl;
    return;
  }
  shell_->UpdateAndSync();
  shell_->SyncTextureToGpu();
  EnsureDescriptorSet();
  if (descriptorSet_ == VK_NULL_HANDLE) {
    std::cout << "[HtmlShellUIHost] UpdateAndSync: descriptorSet_ still null after EnsureDescriptorSet" << std::endl;
  }
}

void HtmlShellUIHost::Draw(VkCommandBuffer cmd, VkExtent2D extent) {
  if (!shell_ || pipeline_ == VK_NULL_HANDLE || descriptorSet_ == VK_NULL_HANDLE) return;

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(extent.width);
  viewport.height = static_cast<float>(extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  VkRect2D scissor{{0, 0}, extent};

  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
  vkCmdDraw(cmd, 3, 1, 0, 0);
}

EditorMessageBridge& HtmlShellUIHost::Bridge() { return bridge_; }

void HtmlShellUIHost::SetScene(Scene* scenePtr) { scene_ = scenePtr; }
void HtmlShellUIHost::SetAssetsRoot(std::filesystem::path assetsRoot) {
  assetsRoot_ = std::move(assetsRoot);
  assetsListCacheValid_ = false; // Invalidate cache when assets root changes
}
void HtmlShellUIHost::SetViewportRect(int x, int y, int w, int h) {
  viewportRect_.x = x;
  viewportRect_.y = y;
  viewportRect_.w = w;
  viewportRect_.h = h;
}

HtmlShellUIHost::ViewportRect HtmlShellUIHost::GetViewportRect() const {
  return viewportRect_;
}

bool HtmlShellUIHost::IsPointInViewport(double x, double y) const {
  if (viewportRect_.w <= 0 || viewportRect_.h <= 0) return false;
  return x >= viewportRect_.x && y >= viewportRect_.y &&
         x <= (viewportRect_.x + viewportRect_.w) &&
         y <= (viewportRect_.y + viewportRect_.h);
}

void HtmlShellUIHost::OnMouseMove(double x, double y) {
  if (!shell_) return;
  if (IsPointInViewport(x, y)) return; // allow native viewport input
  int ix = static_cast<int>(x);
  int iy = static_cast<int>(y);
  // Only fire if position actually changed
  if (ix == lastMouseX_ && iy == lastMouseY_) return;
  lastMouseX_ = ix;
  lastMouseY_ = iy;
  shell_->FireMouseMove(ix, iy);
}

void HtmlShellUIHost::OnMouseButton(int button, int action, int mods, double x, double y) {
  (void)mods;
  if (!shell_) return;
  if (IsPointInViewport(x, y)) return;
  const int btn = (button == 0) ? 1 : 2;
  if (action == 1) {
    shell_->FireMouseDown(static_cast<int>(x), static_cast<int>(y), btn);
  } else if (action == 0) {
    shell_->FireMouseUp(static_cast<int>(x), static_cast<int>(y), btn);
  }
}

void HtmlShellUIHost::OnScroll(double x, double y, double dx, double dy) {
  if (!shell_) return;
  if (IsPointInViewport(x, y)) return;
  shell_->FireScroll(static_cast<int>(x), static_cast<int>(y), static_cast<int>(dx * 60.0), static_cast<int>(dy * 60.0));
}

static int MapGlfwKeyToUltralight(int key) {
  // Minimal mapping for common keys
  switch (key) {
    case 257: return ultralight::KeyCodes::GK_RETURN; // GLFW_KEY_ENTER
    case 258: return ultralight::KeyCodes::GK_TAB;
    case 259: return ultralight::KeyCodes::GK_BACK;
    case 261: return ultralight::KeyCodes::GK_DELETE;
    case 262: return ultralight::KeyCodes::GK_RIGHT;
    case 263: return ultralight::KeyCodes::GK_LEFT;
    case 264: return ultralight::KeyCodes::GK_DOWN;
    case 265: return ultralight::KeyCodes::GK_UP;
    case 268: return ultralight::KeyCodes::GK_HOME;
    case 269: return ultralight::KeyCodes::GK_END;
    case 256: return ultralight::KeyCodes::GK_ESCAPE;
    default:
      if (key >= 65 && key <= 90) return key; // A-Z
      if (key >= 48 && key <= 57) return key; // 0-9
      return key;
  }
}

void HtmlShellUIHost::OnKey(int key, int scancode, int action, int mods) {
  (void)scancode;
  if (!shell_) return;
  unsigned modifiers = 0;
  if (mods & 0x0001) modifiers |= ultralight::KeyEvent::kMod_ShiftKey;
  if (mods & 0x0002) modifiers |= ultralight::KeyEvent::kMod_CtrlKey;
  if (mods & 0x0004) modifiers |= ultralight::KeyEvent::kMod_AltKey;
  if (mods & 0x0008) modifiers |= ultralight::KeyEvent::kMod_MetaKey;

  int ulKey = MapGlfwKeyToUltralight(key);
  if (action == 1) {
    shell_->FireKey(ulKey, true, modifiers);
  } else if (action == 0) {
    shell_->FireKey(ulKey, false, modifiers);
  }
}

void HtmlShellUIHost::OnChar(unsigned int codepoint) {
  if (!shell_) return;
  shell_->FireChar(static_cast<char32_t>(codepoint));
}

std::vector<char> HtmlShellUIHost::ReadFile(const std::string& filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    std::string alt = "../" + filename;
    file.open(alt, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
      throw std::runtime_error("Failed to open file: " + filename);
    }
  }
  size_t fileSize = (size_t)file.tellg();
  std::vector<char> buffer(fileSize);
  file.seekg(0);
  file.read(buffer.data(), fileSize);
  file.close();
  return buffer;
}

VkShaderModule HtmlShellUIHost::CreateShaderModule(const std::vector<char>& code) {
  VkShaderModuleCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  ci.codeSize = code.size();
  ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

  VkShaderModule module;
  if (vkCreateShaderModule(vkdev_->device, &ci, nullptr, &module) != VK_SUCCESS)
    throw std::runtime_error("Failed to create shader module");
  return module;
}

void HtmlShellUIHost::CreatePipeline(VkRenderPass renderPass, VkExtent2D extent) {
  VkDescriptorPoolSize poolSizes[] = {
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}
  };
  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = poolSizes;
  poolInfo.maxSets = 2;
  vkCreateDescriptorPool(vkdev_->device, &poolInfo, nullptr, &descriptorPool_);

  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  setInfo.bindingCount = 1;
  setInfo.pBindings = &binding;
  vkCreateDescriptorSetLayout(vkdev_->device, &setInfo, nullptr, &descriptorSetLayout_);

  VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &descriptorSetLayout_;
  vkCreatePipelineLayout(vkdev_->device, &layoutInfo, nullptr, &pipelineLayout_);

  auto vertCode = ReadFile("shaders/ui_fullscreen.vert.spv");
  auto fragCode = ReadFile("shaders/ui_fullscreen.frag.spv");
  VkShaderModule vertShader = CreateShaderModule(vertCode);
  VkShaderModule fragShader = CreateShaderModule(fragCode);

  VkPipelineShaderStageCreateInfo vertStage{};
  vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertStage.module = vertShader;
  vertStage.pName = "main";

  VkPipelineShaderStageCreateInfo fragStage{};
  fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragStage.module = fragShader;
  fragStage.pName = "main";

  VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount = 0;
  vertexInput.vertexAttributeDescriptionCount = 0;

  VkPipelineInputAssemblyStateCreateInfo inputAsm{};
  inputAsm.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo raster{};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo msaa{};
  msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_TRUE;
  // Ultralight uses premultiplied alpha, so source color is already multiplied by alpha
  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo colorBlend{};
  colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlend.attachmentCount = 1;
  colorBlend.pAttachments = &colorBlendAttachment;

  std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dynamic{};
  dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamic.pDynamicStates = dynamicStates.data();

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = stages;
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAsm;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &raster;
  pipelineInfo.pMultisampleState = &msaa;
  pipelineInfo.pColorBlendState = &colorBlend;
  pipelineInfo.pDynamicState = &dynamic;
  pipelineInfo.layout = pipelineLayout_;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  vkCreateGraphicsPipelines(vkdev_->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);

  vkDestroyShaderModule(vkdev_->device, vertShader, nullptr);
  vkDestroyShaderModule(vkdev_->device, fragShader, nullptr);

  extent_ = extent;
}

void HtmlShellUIHost::DestroyPipeline() {
  if (pipeline_ != VK_NULL_HANDLE) {
    vkDestroyPipeline(vkdev_->device, pipeline_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
  }
  if (pipelineLayout_ != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(vkdev_->device, pipelineLayout_, nullptr);
    pipelineLayout_ = VK_NULL_HANDLE;
  }
  if (descriptorSetLayout_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(vkdev_->device, descriptorSetLayout_, nullptr);
    descriptorSetLayout_ = VK_NULL_HANDLE;
  }
  if (descriptorPool_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(vkdev_->device, descriptorPool_, nullptr);
    descriptorPool_ = VK_NULL_HANDLE;
  }
  descriptorSet_ = VK_NULL_HANDLE;
}

void HtmlShellUIHost::EnsureDescriptorSet() {
  if (descriptorSet_ != VK_NULL_HANDLE) return;
  if (!shell_) return;
  VulkanTexture* tex = shell_->GetTexture();
  if (!tex || tex->imageView == VK_NULL_HANDLE || tex->sampler == VK_NULL_HANDLE) return;

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool_;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &descriptorSetLayout_;
  vkAllocateDescriptorSets(vkdev_->device, &allocInfo, &descriptorSet_);

  VkDescriptorImageInfo imgInfo{};
  imgInfo.sampler = tex->sampler;
  imgInfo.imageView = tex->imageView;
  imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = descriptorSet_;
  write.dstBinding = 0;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.descriptorCount = 1;
  write.pImageInfo = &imgInfo;

  vkUpdateDescriptorSets(vkdev_->device, 1, &write, 0, nullptr);
}

} // namespace Hephaestus::HtmlUI
