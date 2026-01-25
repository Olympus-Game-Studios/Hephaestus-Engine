#include "HtmlEditorUIHost.h"

#include "UltralightPlatformSetup.h"

#include "../../Scene/Scene.h"
#include "../../Scene/SceneSerializer.h"
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
#include <fstream>
#include <ctime>
#include <iomanip>
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

static JsonValue* EnsureObjectField(JsonValue& obj, const std::string& key) {
  if (!obj.IsObject()) {
    obj.type = JsonValue::Type::Object;
    obj.objectValue.clear();
  }
  auto it = obj.objectValue.find(key);
  if (it == obj.objectValue.end()) {
    auto [newIt, _] = obj.objectValue.emplace(key, MakeObject());
    return &newIt->second;
  }
  if (!it->second.IsObject()) {
    it->second = MakeObject();
  }
  return &it->second;
}

static JsonValue* EnsureArrayField(JsonValue& obj, const std::string& key) {
  if (!obj.IsObject()) {
    obj.type = JsonValue::Type::Object;
    obj.objectValue.clear();
  }
  auto it = obj.objectValue.find(key);
  if (it == obj.objectValue.end()) {
    auto [newIt, _] = obj.objectValue.emplace(key, MakeArray());
    return &newIt->second;
  }
  if (!it->second.IsArray()) {
    it->second = MakeArray();
  }
  return &it->second;
}

static JsonValue* EnsureComponentsArray(JsonValue& node) {
  if (!node.IsObject()) {
    node.type = JsonValue::Type::Object;
    node.objectValue.clear();
  }

  auto it = node.objectValue.find("components");
  if (it == node.objectValue.end()) {
    auto [newIt, _] = node.objectValue.emplace("components", MakeArray());
    return &newIt->second;
  }

  if (it->second.IsArray()) {
    return &it->second;
  }

  if (it->second.IsObject()) {
    JsonValue converted = MakeArray();
    for (const auto& [key, entry] : it->second.objectValue) {
      JsonValue compEntry = MakeObject();
      compEntry.objectValue["type"] = MakeString(key);
      compEntry.objectValue["enabled"] = MakeBool(true);
      compEntry.objectValue["data"] = entry;
      converted.arrayValue.push_back(std::move(compEntry));
    }
    it->second = std::move(converted);
    return &it->second;
  }

  it->second = MakeArray();
  return &it->second;
}

static JsonValue* FindComponentEntry(JsonValue& node, const std::string& type) {
  JsonValue* components = EnsureComponentsArray(node);
  for (auto& entry : components->arrayValue) {
    if (!entry.IsObject()) continue;
    const JsonValue* typeVal = entry.Find("type");
    if (typeVal && typeVal->IsString() && typeVal->AsString() == type) {
      return &entry;
    }
  }
  return nullptr;
}

static JsonValue* EnsureComponentEntry(JsonValue& node, const std::string& type) {
  if (JsonValue* existing = FindComponentEntry(node, type)) {
    return existing;
  }

  JsonValue* components = EnsureComponentsArray(node);
  JsonValue entry = MakeObject();
  entry.objectValue["type"] = MakeString(type);
  entry.objectValue["enabled"] = MakeBool(true);
  entry.objectValue["data"] = MakeObject();
  components->arrayValue.push_back(std::move(entry));
  return &components->arrayValue.back();
}

static JsonValue* FindNodeById(JsonValue& root, const std::string& nodeId) {
  if (!root.IsObject()) return nullptr;
  JsonValue* nodes = EnsureArrayField(root, "nodes");
  for (auto& nodeVal : nodes->arrayValue) {
    if (!nodeVal.IsObject()) continue;
    const JsonValue* idVal = nodeVal.Find("id");
    if (idVal && idVal->IsString() && idVal->AsString() == nodeId) {
      return &nodeVal;
    }
  }
  return nullptr;
}

static JsonValue* FindRootNode(JsonValue& root) {
  if (!root.IsObject()) return nullptr;
  if (const JsonValue* rootNodeVal = root.Find("rootNodeId")) {
    if (rootNodeVal->IsString()) {
      if (JsonValue* rootNode = FindNodeById(root, rootNodeVal->AsString())) {
        return rootNode;
      }
    }
  }
  JsonValue* nodes = EnsureArrayField(root, "nodes");
  for (auto& nodeVal : nodes->arrayValue) {
    if (!nodeVal.IsObject()) continue;
    const JsonValue* parentVal = nodeVal.Find("parentId");
    if (!parentVal || parentVal->IsNull() || (parentVal->IsString() && parentVal->AsString().empty())) {
      return &nodeVal;
    }
  }
  return nodes->arrayValue.empty() ? nullptr : &nodes->arrayValue.front();
}

static std::string NormalizeBlueprintKey(const std::filesystem::path& path,
                                         const std::filesystem::path& root) {
  std::filesystem::path norm = path.lexically_normal();
  if (!root.empty()) {
    std::error_code ec;
    auto rel = std::filesystem::relative(norm, root, ec);
    if (!ec) {
      auto relStr = rel.generic_string();
      if (!relStr.empty() && relStr.find("..") != 0) {
        return relStr;
      }
    }
  }
  return norm.generic_string();
}

static bool ResolveBlueprintPath(const std::filesystem::path& input,
                                 const std::filesystem::path& assetsRoot,
                                 std::filesystem::path& outAbs,
                                 std::string& outError) {
  outError.clear();
  std::filesystem::path candidate = input;
  if (!candidate.is_absolute()) {
    if (assetsRoot.empty()) {
      outError = "no_assets_root";
      return false;
    }
    candidate = assetsRoot / candidate;
  }
  outAbs = candidate.lexically_normal();

  if (!assetsRoot.empty()) {
    auto absStr = outAbs.generic_string();
    auto rootStr = assetsRoot.lexically_normal().generic_string();
    if (absStr.size() < rootStr.size() || absStr.compare(0, rootStr.size(), rootStr) != 0) {
      outError = "path_escape";
      return false;
    }
  }
  return true;
}

static std::string ReadFileText(const std::filesystem::path& path, std::string& outError) {
  std::ifstream in(path);
  if (!in.is_open()) {
    outError = "Unable to open blueprint file.";
    return {};
  }
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  return content;
}

static bool ParseJsonPayload(const std::string& payload, JsonValue& outRoot, std::string& outError) {
  JsonParser parser(payload);
  return parser.Parse(outRoot, outError);
}

static void WriteJsonIndent(std::ostream& out, int indent) {
  for (int i = 0; i < indent; ++i) out << ' ';
}

static void WriteJsonValuePretty(std::ostream& out, const JsonValue& value, int indent) {
  switch (value.type) {
    case JsonValue::Type::Null:
      out << "null";
      return;
    case JsonValue::Type::Bool:
      out << (value.boolValue ? "true" : "false");
      return;
    case JsonValue::Type::Number:
      out << std::setprecision(10) << value.numberValue;
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
      json << "{";
      json << "\"id\":\"" << JsonEscape(id) << "\",";
      json << "\"name\":\"" << JsonEscape(nodeName) << "\",";
      json << "\"parentId\":\"" << JsonEscape(parentId) << "\",";
      json << "\"transform\":{";
      json << "\"position\":[" << px << "," << py << "," << pz << "],";
      json << "\"rotation\":[" << rx << "," << ry << "," << rz << "],";
      json << "\"scale\":[" << sx << "," << sy << "," << sz << "]";
      json << "},";

      json << "\"components\":[";
      bool firstComp = true;
      bool hasScript = false;
      if (const JsonValue* compsVal = nodeVal.Find("components")) {
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

bool HtmlEditorUIHost::LoadBlueprintDocumentFromDisk(const std::filesystem::path& path,
                                                     HtmlEditorUIHost::BlueprintDocument& outDoc,
                                                     std::string& outError) {
  outError.clear();
  std::string content = ReadFileText(path, outError);
  if (!outError.empty()) {
    return false;
  }

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

static std::string SanitizeAssetName(const std::string& value) {
  std::string trimmed = value;
  while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) {
    trimmed.erase(trimmed.begin());
  }
  while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
    trimmed.pop_back();
  }
  if (trimmed.empty()) {
    return std::string();
  }

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
  static uint32_t counter = 0;
  std::ostringstream oss;
  oss << std::hex << static_cast<uint64_t>(std::time(nullptr)) << "-" << counter++;
  return oss.str();
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

void HtmlEditorUIHost::SetBlueprintMode(bool enabled) {
  if (blueprintMode_ != enabled) {
    blueprintMode_ = enabled;
    themeDirty_ = true;
  }
}

void HtmlEditorUIHost::SetAssetOpenCallback(AssetOpenCallback callback) {
  assetOpenCallback_ = std::move(callback);
}

void HtmlEditorUIHost::SetAssetCreateCallback(AssetOpenCallback callback) {
  assetCreateCallback_ = std::move(callback);
}

void HtmlEditorUIHost::OpenBlueprintEditor(const std::filesystem::path& path) {
  std::filesystem::path absPath;
  std::string resolveError;
  if (!ResolveBlueprintPath(path, assetsRoot_, absPath, resolveError)) {
    std::ostringstream payload;
    payload << "{\"error\":\"" << JsonEscape(resolveError) << "\"}";
    bridge_.EmitEvent("blueprint.open_failed", payload.str());
    return;
  }

  std::string key = NormalizeBlueprintKey(absPath, assetsRoot_);
  auto it = blueprintDocs_.find(key);
  if (it == blueprintDocs_.end()) {
    BlueprintDocument doc;
    std::string loadError;
    if (!LoadBlueprintDocumentFromDisk(absPath, doc, loadError)) {
      std::ostringstream payload;
      payload << "{\"path\":\"" << JsonEscape(key) << "\",\"error\":\"" << JsonEscape(loadError) << "\"}";
      bridge_.EmitEvent("blueprint.open_failed", payload.str());
      return;
    }
    blueprintDocs_[key] = std::move(doc);
    blueprintTabOrder_.push_back(key);
    it = blueprintDocs_.find(key);
  }

  std::ostringstream payload;
  payload << BuildBlueprintDocJson(key, it->second.root, it->second.dirty);
  bridge_.EmitEvent("blueprint.open", payload.str());
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
      if (e.HasComponent<EditorOnlyComponent>()) {
        continue;
      }
      if (blueprintMode_ && !e.HasComponent<BlueprintNodeComponent>()) {
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
    lastAssetDragPath_ = *pathOpt;
    assetDragEndFrameCountdown_ = 0;  // Reset countdown when new drag starts
    return std::string("{\"ok\":true}");
  });

  bridge_.RegisterHandler("assets.drag_end", [this](const std::string& payload) -> std::string {
    (void)payload;
    // Don't immediately clear lastAssetDragPath_ - set countdown to persist for a few frames
    // This ensures ImGui has time to process the drop target
    if (pendingAssetDrag_) {
      assetDragEndFrameCountdown_ = 3;  // Keep drag source active for 3 more frames
    }
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

    // New-style creation: kind + dir + name.
    auto kindOpt = EditorMessageBridge::ParseStringField(payload, "kind");
    if (!kindOpt) {
      // Legacy path-only creation callback.
      std::filesystem::path rel = ".";
      if (auto p = EditorMessageBridge::ParseStringField(payload, "path")) {
        rel = std::filesystem::path(*p);
      }

      std::filesystem::path assetPath = (assetsRoot_ / rel).lexically_normal();
      auto pathStr = assetPath.generic_string();
      auto rootStr = assetsRoot_.lexically_normal().generic_string();
      if (pathStr.size() < rootStr.size() || pathStr.compare(0, rootStr.size(), rootStr) != 0) {
        return std::string("{\"ok\":false,\"error\":\"path_escape\"}");
      }

      if (assetCreateCallback_) {
        assetCreateCallback_(assetPath);
      }

      return std::string("{\"ok\":true}");
    }

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
    if (!nameOpt || nameOpt->empty()) {
      return std::string("{\"ok\":false,\"error\":\"missing_name\"}");
    }

    std::string sanitized = SanitizeAssetName(*nameOpt);
    if (sanitized.empty()) {
      return std::string("{\"ok\":false,\"error\":\"invalid_name\"}");
    }

    const std::string& kind = *kindOpt;
    std::error_code createErr;
    std::filesystem::create_directories(dirPath, createErr);
    if (createErr) {
      return std::string("{\"ok\":false,\"error\":\"create_directory_failed\"}");
    }

    auto makeRel = [&](const std::filesystem::path& p) {
      std::error_code relEc;
      auto rp = std::filesystem::relative(p, assetsRoot_, relEc);
      return relEc ? p.filename().string() : rp.generic_string();
    };

    if (kind == "folder") {
      std::filesystem::path folderPath = dirPath / sanitized;
      if (std::filesystem::exists(folderPath)) {
        return std::string("{\"ok\":false,\"error\":\"already_exists\"}");
      }
      std::filesystem::create_directories(folderPath, createErr);
      if (createErr) {
        return std::string("{\"ok\":false,\"error\":\"create_folder_failed\"}");
      }
      std::ostringstream json;
      json << "{\"ok\":true,\"path\":\"" << JsonEscape(makeRel(folderPath)) << "\"}";
      return json.str();
    }

    if (kind == "material") {
      std::filesystem::path materialPath = dirPath / (sanitized + ".hematerial");
      if (std::filesystem::exists(materialPath)) {
        return std::string("{\"ok\":false,\"error\":\"already_exists\"}");
      }
      std::ofstream out(materialPath, std::ios::trunc);
      if (!out) {
        return std::string("{\"ok\":false,\"error\":\"write_failed\"}");
      }
      out << "{\n";
      out << "  \"name\": \"" << sanitized << "\"\n";
      out << "}\n";
      out.close();

      std::ostringstream json;
      json << "{\"ok\":true,\"path\":\"" << JsonEscape(makeRel(materialPath)) << "\"}";
      return json.str();
    }

    if (kind == "lua_script") {
      std::filesystem::path scriptPath = dirPath / (sanitized + ".lua");
      if (std::filesystem::exists(scriptPath)) {
        return std::string("{\"ok\":false,\"error\":\"already_exists\"}");
      }
      std::ofstream out(scriptPath, std::ios::trunc);
      if (!out) {
        return std::string("{\"ok\":false,\"error\":\"write_failed\"}");
      }
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
      json << "{\"ok\":true,\"path\":\"" << JsonEscape(makeRel(scriptPath)) << "\"}";
      return json.str();
    }

    if (kind == "cpp_script") {
      std::filesystem::path headerPath = dirPath / (sanitized + ".h");
      std::filesystem::path implPath = dirPath / (sanitized + ".cpp");
      if (std::filesystem::exists(headerPath) || std::filesystem::exists(implPath)) {
        return std::string("{\"ok\":false,\"error\":\"already_exists\"}");
      }
      std::ofstream headerFile(headerPath, std::ios::trunc);
      if (!headerFile) {
        return std::string("{\"ok\":false,\"error\":\"write_failed\"}");
      }
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
      json << "{\"ok\":true,\"path\":\"" << JsonEscape(makeRel(headerPath)) << "\"}";
      return json.str();
    }

    if (kind == "blueprint") {
      std::filesystem::path blueprintPath = dirPath / (sanitized + ".hpblueprint");
      if (std::filesystem::exists(blueprintPath)) {
        return std::string("{\"ok\":false,\"error\":\"already_exists\"}");
      }
      std::ofstream out(blueprintPath, std::ios::trunc);
      if (!out) {
        return std::string("{\"ok\":false,\"error\":\"write_failed\"}");
      }
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
      json << "{\"ok\":true,\"path\":\"" << JsonEscape(makeRel(blueprintPath)) << "\"}";
      return json.str();
    }

    if (kind == "scene") {
      std::filesystem::path scenePath = dirPath / (sanitized + ".hpscene");
      if (std::filesystem::exists(scenePath)) {
        return std::string("{\"ok\":false,\"error\":\"already_exists\"}");
      }

      Scene scene(sanitized);
      SceneSerializer serializer(scene);
      std::string serializeError;
      if (!serializer.Serialize(scenePath, serializeError)) {
        return std::string("{\"ok\":false,\"error\":\"write_failed\",\"detail\":\"") + JsonEscape(serializeError) + "\"}";
      }

      std::ostringstream json;
      json << "{\"ok\":true,\"path\":\"" << JsonEscape(makeRel(scenePath)) << "\"}";
      return json.str();
    }

    return std::string("{\"ok\":false,\"error\":\"unknown_kind\"}");
  });

  bridge_.RegisterHandler("blueprint.load", [this](const std::string& payload) -> std::string {
    auto pathOpt = EditorMessageBridge::ParseStringField(payload, "path");
    if (!pathOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_path\"}");
    }

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
      if (nameVal->IsString()) {
        nodeObj->objectValue["name"] = MakeString(nameVal->AsString());
      }
    }
    if (const JsonValue* parentVal = patchVal->Find("parentId")) {
      if (parentVal->IsNull()) {
        nodeObj->objectValue["parentId"] = MakeNull();
      } else if (parentVal->IsString()) {
        nodeObj->objectValue["parentId"] = MakeString(parentVal->AsString());
      }
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
    if (nodeVal && nodeVal->IsString()) {
      nodeObj = FindNodeById(it->second.root, nodeVal->AsString());
    }
    if (!nodeObj) {
      nodeObj = FindRootNode(it->second.root);
    }
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
    if (!pathVal || !pathVal->IsString()) {
      return std::string("{\"ok\":false,\"error\":\"missing_path\"}");
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
    std::string parentId = (parentVal && parentVal->IsString()) ? parentVal->AsString() : std::string();
    std::string newId = std::string("node_") + GenerateBlueprintId();

    JsonValue newNode = MakeObject();
    newNode.objectValue["id"] = MakeString(newId);
    newNode.objectValue["name"] = MakeString("NewNode");
    if (parentId.empty()) {
      newNode.objectValue["parentId"] = MakeNull();
    } else {
      newNode.objectValue["parentId"] = MakeString(parentId);
    }
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

    const std::string compType = compVal->AsString();
    if (!FindComponentEntry(*nodeObj, compType)) {
      EnsureComponentEntry(*nodeObj, compType);
      it->second.dirty = true;
    }

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
    if (!pathVal || !pathVal->IsString() || !nodeVal || !nodeVal->IsString() || !compVal || !compVal->IsString() || !dataVal) {
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

    JsonValue newComponent = MakeObject();
    if (dataVal->IsString()) {
      JsonValue parsed;
      std::string compErr;
      JsonParser parser(dataVal->AsString());
      if (!parser.Parse(parsed, compErr)) {
        return std::string("{\"ok\":false,\"error\":\"component_parse_failed\",\"detail\":\"") + JsonEscape(compErr) + "\"}";
      }
      newComponent = std::move(parsed);
    } else {
      newComponent = *dataVal;
    }

    JsonValue* compEntry = EnsureComponentEntry(*nodeObj, compVal->AsString());
    compEntry->objectValue["type"] = MakeString(compVal->AsString());
    compEntry->objectValue["data"] = std::move(newComponent);
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

    // Prevent deleting root node.
    JsonValue* nodeObj = FindNodeById(it->second.root, nodeId);
    if (!nodeObj) {
      return std::string("{\"ok\":false,\"error\":\"node_not_found\"}");
    }
    const JsonValue* parentVal = nodeObj->Find("parentId");
    if (!parentVal || parentVal->IsNull() || (parentVal->IsString() && parentVal->AsString().empty())) {
      return std::string("{\"ok\":false,\"error\":\"cannot_delete_root\"}");
    }

    // Reparent children to root.
    for (auto& n : nodes->arrayValue) {
      if (!n.IsObject()) continue;
      const JsonValue* p = n.Find("parentId");
      if (p && p->IsString() && p->AsString() == nodeId) {
        n.objectValue["parentId"] = MakeNull();
      }
    }

    // Remove the node.
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

  bridge_.RegisterHandler("blueprint.save", [this](const std::string& payload) -> std::string {
    auto pathOpt = EditorMessageBridge::ParseStringField(payload, "path");
    if (!pathOpt) {
      return std::string("{\"ok\":false,\"error\":\"missing_path\"}");
    }

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

    std::ofstream out(absPath, std::ios::trunc);
    if (!out) {
      return std::string("{\"ok\":false,\"error\":\"write_failed\"}");
    }

    WriteJsonValuePretty(out, it->second.root, 0);
    out << "\n";
    out.close();

    if (!out.good()) {
      return std::string("{\"ok\":false,\"error\":\"write_failed\"}");
    }

    it->second.dirty = false;
    std::ostringstream json;
    json << "{\"ok\":true,\"doc\":" << BuildBlueprintDocJson(key, it->second.root, it->second.dirty) << "}";
    return json.str();
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
    json << "\"submeshCount\":" << mesh.submeshCount << ",";
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
  blueprintEditor_ = std::make_unique<HtmlPanelView>("Blueprint Editor", &bridge_, vkdev_, imguiDescriptorPool_);
  logConsole_ = std::make_unique<HtmlPanelView>("Log Console", &bridge_, vkdev_, imguiDescriptorPool_);

  // Initial sizes will be overridden by ImGui window sizes.
  // These paths are resolved relative to the FileSystem base (exeDir).
  outliner_->Create(renderer_, 400, 600, "file:///editor/assets/ui/panels/outliner.html");
  addActors_->Create(renderer_, 520, 650, "file:///editor/assets/ui/panels/add_actors.html");
  details_->Create(renderer_, 400, 600, "file:///editor/assets/ui/panels/details.html");
  assetBrowser_->Create(renderer_, 800, 300, "file:///editor/assets/ui/panels/asset_browser.html");
  blueprintEditor_->Create(renderer_, 1200, 800, "file:///editor/assets/ui/panels/blueprint_editor.html");
  logConsole_->Create(renderer_, 800, 300, "file:///editor/assets/ui/panels/log_console.html");

  // Default JS event emitter targets log console view.
  bridge_.SetSendToJs([this](const std::string& jsCall) {
    // Broadcast to all panels (keep them in sync).
    if (outliner_) outliner_->EvaluateScript(jsCall);
    if (addActors_) addActors_->EvaluateScript(jsCall);
    if (details_) details_->EvaluateScript(jsCall);
    if (assetBrowser_) assetBrowser_->EvaluateScript(jsCall);
    if (blueprintEditor_) blueprintEditor_->EvaluateScript(jsCall);
    if (logConsole_) logConsole_->EvaluateScript(jsCall);
  });
}

void HtmlEditorUIHost::ApplyThemeToPanels() {
  if (!themeDirty_) {
    return;
  }

  static const char* kSceneThemeScript = R"JS(
(() => {
  const root = document.documentElement;
  root.setAttribute('data-editor-mode', 'scene');
  root.style.setProperty('--accent', '#2d8cff');
  root.style.setProperty('--accentHover', '#4a9dff');
  root.style.setProperty('--accentSoft', '#5ba7ff');
  root.style.setProperty('--accent2', '#f5b76c');
  root.style.setProperty('--accent2Soft', '#5a4828');
})();
)JS";

  static const char* kBlueprintThemeScript = R"JS(
(() => {
  const root = document.documentElement;
  root.setAttribute('data-editor-mode', 'blueprint');
  root.style.setProperty('--accent', '#a56bff');
  root.style.setProperty('--accentHover', '#b984ff');
  root.style.setProperty('--accentSoft', '#caa6ff');
  root.style.setProperty('--accent2', '#60d6c5');
  root.style.setProperty('--accent2Soft', '#204a45');
})();
)JS";

  const char* script = blueprintMode_ ? kBlueprintThemeScript : kSceneThemeScript;
  if (outliner_) outliner_->EvaluateScript(script);
  if (addActors_) addActors_->EvaluateScript(script);
  if (details_) details_->EvaluateScript(script);
  if (assetBrowser_) assetBrowser_->EvaluateScript(script);
  if (blueprintEditor_) blueprintEditor_->EvaluateScript(script);
  if (logConsole_) logConsole_->EvaluateScript(script);

  themeDirty_ = false;
}

void HtmlEditorUIHost::UpdateAndSync(float dt) {
  (void)dt;
  if (!renderer_)
    return;

  ApplyThemeToPanels();

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
  if (blueprintEditor_ && blueprintEditorVisible_) blueprintEditor_->SyncTextureToGpu();
  if (logConsole_ && logConsoleVisible_) logConsole_->SyncTextureToGpu();
}

void HtmlEditorUIHost::DrawDockedPanels() {
  DrawOutlinerPanel();
  DrawAddActorsPanel();
  DrawDetailsPanel();
  DrawAssetBrowserPanel();
  DrawBlueprintEditorPanel();
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
    // Use lastAssetDragPath_ with countdown to ensure drag persists long enough for drop processing.
    // Use SourceExtern flag to create an external drag source that doesn't require hovering an ImGui item.
    bool hasDragPath = pendingAssetDrag_.has_value() ||
               (assetDragEndFrameCountdown_ > 0 && !lastAssetDragPath_.empty());
    bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool panelHovered = assetBrowser_->IsHovered();
    
    if (hasDragPath && mouseDown && panelHovered) {
      if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
        const std::string& assetPath = pendingAssetDrag_.has_value() ? *pendingAssetDrag_ : lastAssetDragPath_;
        ImGui::SetDragDropPayload("ASSET_ITEM", assetPath.c_str(),
                                  assetPath.size() + 1);
        ImGui::TextUnformatted(assetPath.c_str());
        ImGui::EndDragDropSource();
      }
    }
    
    // Decrement countdown and clear when expired
    if (assetDragEndFrameCountdown_ > 0) {
      assetDragEndFrameCountdown_--;
      if (assetDragEndFrameCountdown_ == 0) {
        lastAssetDragPath_.clear();
      }
    }

    assetBrowser_->HandleImGuiInput();
  }
  ImGui::End();
}

void HtmlEditorUIHost::DrawBlueprintEditorPanel() {
  if (!blueprintEditor_)
    return;
  blueprintEditorVisible_ = false;
  if (ImGui::Begin("Blueprint Editor")) {
    blueprintEditorVisible_ = true;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    blueprintEditor_->Resize(static_cast<uint32_t>(std::max(1.0f, avail.x)),
                             static_cast<uint32_t>(std::max(1.0f, avail.y)));
    blueprintEditor_->DrawImGuiImage();
    blueprintEditor_->HandleImGuiInput();
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

bool HtmlEditorUIHost::IsAnyPanelHovered() const {
  if (outliner_ && outliner_->IsHovered()) return true;
  if (addActors_ && addActors_->IsHovered()) return true;
  if (details_ && details_->IsHovered()) return true;
  if (assetBrowser_ && assetBrowser_->IsHovered()) return true;
  if (blueprintEditor_ && blueprintEditor_->IsHovered()) return true;
  if (logConsole_ && logConsole_->IsHovered()) return true;
  return false;
}

EditorMessageBridge& HtmlEditorUIHost::Bridge() { return bridge_; }

} // namespace Hephaestus::HtmlUI
