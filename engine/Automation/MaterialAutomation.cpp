#include "MaterialAutomation.h"

#include "AutomationToolSupport.h"

#include "Assets/AssetManager.h"
#include "Scene/PrefabOverrideSystem.h"
#include "UI/MaterialEditor/MaterialAssetSerializer.h"
#include "UI/MaterialEditor/MaterialGraph.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

namespace Hephaestus::Automation {
namespace {

using namespace Content;

ToolOutcome Failure(ErrorCode code, std::string message)
{
    return ToolResult::Failure(code, std::move(message));
}

Json Vec3Json(const Vec3& value)
{
    return Json::array({value.x, value.y, value.z});
}

Json MaterialToJson(const MaterialComponent& material)
{
    return Json{
        {"albedo", Vec3Json(material.albedo)},
        {"metallic", material.metallic},
        {"roughness", material.roughness},
        {"ao", material.ao},
        {"emissive", Vec3Json(material.emissive)},
        {"emissiveStrength", material.emissiveStrength},
        {"reflectionStrength", material.reflectionStrength},
        {"specular", material.specular},
        {"opacity", material.opacity},
        {"opacityMask", material.opacityMask},
        {"refraction", material.refraction},
        {"albedoTexturePath", material.albedoTexturePath},
        {"useAlbedoTexture", material.useAlbedoTexture},
        {"normalTexturePath", material.normalTexturePath},
        {"useNormalTexture", material.useNormalTexture},
        {"roughnessTexturePath", material.roughnessTexturePath},
        {"useRoughnessTexture", material.useRoughnessTexture},
        {"metallicTexturePath", material.metallicTexturePath},
        {"useMetallicTexture", material.useMetallicTexture},
        {"aoTexturePath", material.aoTexturePath},
        {"useAoTexture", material.useAoTexture},
        {"useWorldAlignedTexture", material.useWorldAlignedTexture},
        {"worldTextureScale", material.worldTextureScale},
    };
}

bool ReadColor(const Json& value, Vec3& out)
{
    if (!value.is_array() || value.size() != 3 ||
        !value[0].is_number() || !value[1].is_number() || !value[2].is_number()) {
        return false;
    }
    out = Vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    return true;
}

bool IsTextureProperty(const std::string& name)
{
    static const char* const kTextures[] = {
        "albedoTexturePath", "normalTexturePath", "roughnessTexturePath",
        "metallicTexturePath", "aoTexturePath",
    };
    return std::find(std::begin(kTextures), std::end(kTextures), name) != std::end(kTextures);
}

bool ApplyMaterialProperties(MaterialComponent& material, const Json& properties,
                             std::string& outError)
{
    if (!properties.is_object()) {
        outError = "properties must be a JSON object";
        return false;
    }
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const std::string& name = it.key();
        const Json& value = it.value();

        auto requireNumber = [&](float& target, float minimum, float maximum) -> bool {
            if (!value.is_number()) {
                outError = "Property '" + name + "' must be a number";
                return false;
            }
            const double number = value.get<double>();
            if (number < minimum || number > maximum) {
                outError = "Property '" + name + "' must be in range [" +
                           std::to_string(minimum) + ", " + std::to_string(maximum) + "]";
                return false;
            }
            target = static_cast<float>(number);
            return true;
        };

        if (name == "albedo") {
            if (!ReadColor(value, material.albedo)) {
                outError = "Property 'albedo' must be a three-number array";
                return false;
            }
        } else if (name == "emissive") {
            if (!ReadColor(value, material.emissive)) {
                outError = "Property 'emissive' must be a three-number array";
                return false;
            }
        } else if (name == "metallic") {
            if (!requireNumber(material.metallic, 0.0f, 1.0f)) return false;
        } else if (name == "roughness") {
            if (!requireNumber(material.roughness, 0.0f, 1.0f)) return false;
        } else if (name == "ao") {
            if (!requireNumber(material.ao, 0.0f, 1.0f)) return false;
        } else if (name == "emissiveStrength") {
            if (!requireNumber(material.emissiveStrength, 0.0f, 1.0e6f)) return false;
        } else if (name == "reflectionStrength") {
            if (!requireNumber(material.reflectionStrength, 0.0f, 1.0f)) return false;
        } else if (name == "specular") {
            if (!requireNumber(material.specular, 0.0f, 1.0f)) return false;
        } else if (name == "opacity") {
            if (!requireNumber(material.opacity, 0.0f, 1.0f)) return false;
        } else if (name == "opacityMask") {
            if (!requireNumber(material.opacityMask, 0.0f, 1.0f)) return false;
        } else if (name == "worldTextureScale") {
            if (!requireNumber(material.worldTextureScale, 0.0f, 1.0e6f)) return false;
        } else if (name == "useWorldAlignedTexture") {
            if (!value.is_boolean()) {
                outError = "Property 'useWorldAlignedTexture' must be a boolean";
                return false;
            }
            material.useWorldAlignedTexture = value.get<bool>();
        } else if (IsTextureProperty(name)) {
            if (!value.is_string()) {
                outError = "Property '" + name + "' must be a string asset path";
                return false;
            }
            const std::string path = value.get<std::string>();
            if (name == "albedoTexturePath") {
                material.albedoTexturePath = path;
                material.useAlbedoTexture = !path.empty();
            } else if (name == "normalTexturePath") {
                material.normalTexturePath = path;
                material.useNormalTexture = !path.empty();
            } else if (name == "roughnessTexturePath") {
                material.roughnessTexturePath = path;
                material.useRoughnessTexture = !path.empty();
            } else if (name == "metallicTexturePath") {
                material.metallicTexturePath = path;
                material.useMetallicTexture = !path.empty();
            } else if (name == "aoTexturePath") {
                material.aoTexturePath = path;
                material.useAoTexture = !path.empty();
            }
        } else if (name == "useAlbedoTexture") {
            if (!value.is_boolean()) {
                outError = "Property 'useAlbedoTexture' must be a boolean";
                return false;
            }
            material.useAlbedoTexture = value.get<bool>();
        } else if (name == "useNormalTexture") {
            if (!value.is_boolean()) {
                outError = "Property 'useNormalTexture' must be a boolean";
                return false;
            }
            material.useNormalTexture = value.get<bool>();
        } else if (name == "useRoughnessTexture") {
            if (!value.is_boolean()) {
                outError = "Property 'useRoughnessTexture' must be a boolean";
                return false;
            }
            material.useRoughnessTexture = value.get<bool>();
        } else if (name == "useMetallicTexture") {
            if (!value.is_boolean()) {
                outError = "Property 'useMetallicTexture' must be a boolean";
                return false;
            }
            material.useMetallicTexture = value.get<bool>();
        } else if (name == "useAoTexture") {
            if (!value.is_boolean()) {
                outError = "Property 'useAoTexture' must be a boolean";
                return false;
            }
            material.useAoTexture = value.get<bool>();
        } else {
            outError = "Unknown material property: " + name;
            return false;
        }
    }
    return true;
}

Hephaestus::AssetManager* RequireAssets(ToolContext& context, ToolResult& outFailure)
{
    Hephaestus::AssetManager* assets = context.Backend().Assets();
    if (!assets || !assets->HasValidAssetRoot()) {
        outFailure = ToolResult::Failure(ErrorCode::ToolUnavailable,
                                         "Asset management is unavailable for this host");
        return nullptr;
    }
    return assets;
}

bool ResolveMaterialPath(Hephaestus::AssetManager& assets, const std::string& input,
                         std::filesystem::path& outAbsolute, std::string& outPortable,
                         std::string& outError)
{
    const auto absolute = ResolveAssetPath(assets.GetProjectRoot(), assets.GetAssetRoot(), input, outError);
    if (!absolute) return false;
    outAbsolute = *absolute;
    outPortable = AssetPathPolicy::ToPortableKey(assets.GetProjectRoot(), absolute->generic_string());
    return true;
}

} // namespace

void RegisterMaterialAutomation(AutomationService& service)
{
    RegisterOrThrow(service, Definition(
        "material.create",
        "Create a valid Hephaestus material asset with current PBR properties.",
        ObjectSchema({{"name", {{"type", "string"}}},
                      {"folder", {{"type", "string"}}},
                      {"properties", {{"type", "object"}}}},
                     Json::array({"name"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            Hephaestus::AssetManager* assets = RequireAssets(context, failure);
            if (!assets) return failure;
            if (!arguments.contains("name") || !arguments["name"].is_string() ||
                arguments["name"].get<std::string>().empty()) {
                return Failure(ErrorCode::InvalidArguments, "A non-empty material name is required");
            }

            std::string name = arguments["name"].get<std::string>();
            if (name.size() < 11 || name.compare(name.size() - 11, 11, ".hematerial") != 0) {
                name += ".hematerial";
            }
            if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
                return Failure(ErrorCode::InvalidArguments,
                               "name must be a plain file name without path separators");
            }

            std::string error;
            const auto folder = ResolveAssetFolder(
                assets->GetAssetRoot(), arguments.value("folder", std::string("assets/materials")),
                error);
            if (!folder) return Failure(ErrorCode::InvalidArguments, error);
            const std::filesystem::path absolute = (*folder / name).lexically_normal();
            std::error_code existsError;
            if (std::filesystem::exists(absolute, existsError)) {
                return Failure(ErrorCode::InvalidArguments,
                               "A material already exists at " + absolute.generic_string());
            }

            MaterialComponent material;
            if (arguments.contains("properties") &&
                !ApplyMaterialProperties(material, arguments["properties"], error)) {
                return (error.rfind("Unknown material property", 0) == 0)
                    ? Failure(ErrorCode::UnknownProperty, error)
                    : Failure(ErrorCode::InvalidArguments, error);
            }

            std::error_code createError;
            std::filesystem::create_directories(*folder, createError);
            if (createError) {
                return Failure(ErrorCode::InternalError,
                               "Failed to create material folder: " + createError.message());
            }

            MaterialEditor::Graph graph;
            MaterialEditor::BuildMinimalGraphFromLegacyPbr(material, graph);
            const std::string displayName =
                std::filesystem::path(name).stem().string();
            if (!MaterialEditor::SaveMaterialAssetCompat(absolute, material, displayName, graph, {},
                                                         error)) {
                return Failure(ErrorCode::InternalError,
                               error.empty() ? "Failed to write material asset" : error);
            }

            assets->Refresh();
            context.Backend().NotifyAssetChanged();
            const std::string portable =
                AssetPathPolicy::ToPortableKey(assets->GetProjectRoot(), absolute.generic_string());
            return ToolResult::Success(
                {{"material", {{"path", portable}, {"type", "material"}, {"displayName", displayName}}},
                 {"properties", MaterialToJson(material)}});
        }, true));

    RegisterOrThrow(service, Definition(
        "material.get",
        "Read current PBR properties of a Hephaestus material asset.",
        ObjectSchema({{"path", {{"type", "string"}}}}, Json::array({"path"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            Hephaestus::AssetManager* assets = RequireAssets(context, failure);
            if (!assets) return failure;
            if (!arguments.contains("path") || !arguments["path"].is_string()) {
                return Failure(ErrorCode::InvalidArguments, "A material path is required");
            }
            std::filesystem::path absolute;
            std::string portable;
            std::string error;
            if (!ResolveMaterialPath(*assets, arguments["path"].get<std::string>(), absolute, portable,
                                     error)) {
                return Failure(ErrorCode::InvalidArguments, error);
            }
            std::error_code existsError;
            if (!std::filesystem::exists(absolute, existsError)) {
                return Failure(ErrorCode::InvalidArguments,
                               "Material not found: " + arguments["path"].get<std::string>());
            }
            MaterialComponent material;
            MaterialEditor::Graph graph;
            std::string displayName;
            if (!MaterialEditor::LoadMaterialAssetCompat(absolute, material, displayName, graph, error)) {
                return Failure(ErrorCode::InternalError,
                               error.empty() ? "Failed to read material asset" : error);
            }
            return ToolResult::Success(
                {{"material", {{"path", portable},
                               {"type", "material"},
                               {"displayName", displayName},
                               {"graphAuthored", graph.HasSourceGraph() && !graph.IsLegacyMode()}}},
                 {"properties", MaterialToJson(material)}});
        }));

    RegisterOrThrow(service, Definition(
        "material.set",
        "Set one or more current PBR properties on a Hephaestus material asset.",
        ObjectSchema({{"path", {{"type", "string"}}},
                      {"properties", {{"type", "object"}}}},
                     Json::array({"path", "properties"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            Hephaestus::AssetManager* assets = RequireAssets(context, failure);
            if (!assets) return failure;
            if (!arguments.contains("path") || !arguments["path"].is_string() ||
                !arguments.contains("properties") || !arguments["properties"].is_object()) {
                return Failure(ErrorCode::InvalidArguments,
                               "path and a properties object are required");
            }
            std::filesystem::path absolute;
            std::string portable;
            std::string error;
            if (!ResolveMaterialPath(*assets, arguments["path"].get<std::string>(), absolute, portable,
                                     error)) {
                return Failure(ErrorCode::InvalidArguments, error);
            }
            std::error_code existsError;
            if (!std::filesystem::exists(absolute, existsError)) {
                return Failure(ErrorCode::InvalidArguments,
                               "Material not found: " + arguments["path"].get<std::string>());
            }

            MaterialComponent material;
            MaterialEditor::Graph graph;
            std::string displayName;
            if (!MaterialEditor::LoadMaterialAssetCompat(absolute, material, displayName, graph, error)) {
                return Failure(ErrorCode::InternalError,
                               error.empty() ? "Failed to read material asset" : error);
            }
            if (!ApplyMaterialProperties(material, arguments["properties"], error)) {
                return (error.rfind("Unknown material property", 0) == 0)
                    ? Failure(ErrorCode::UnknownProperty, error)
                    : Failure(ErrorCode::InvalidArguments, error);
            }
            if (!MaterialEditor::SaveMaterialAssetCompat(absolute, material, displayName, graph, {},
                                                         error)) {
                return Failure(ErrorCode::InternalError,
                               error.empty() ? "Failed to write material asset" : error);
            }
            context.Backend().NotifyAssetChanged();
            return ToolResult::Success(
                {{"material", {{"path", portable},
                               {"type", "material"},
                               {"displayName", displayName},
                               {"graphAuthored", graph.HasSourceGraph() && !graph.IsLegacyMode()}}},
                 {"properties", MaterialToJson(material)},
                 {"saved", true}});
        }, true));

    RegisterOrThrow(service, Definition(
        "material.assign",
        "Assign a material asset to an entity's mesh-renderer material slot.",
        ObjectSchema({{"entity", {{"type", "string"}}},
                      {"path", {{"type", "string"}}},
                      {"slot", {{"type", "integer"}, {"minimum", 0}}}},
                     Json::array({"entity", "path"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto resolved = ResolveEntity(context, arguments, failure);
            if (!resolved) return failure;
            Hephaestus::AssetManager* assets = context.Backend().Assets();
            if (!assets || !assets->HasValidAssetRoot()) {
                return Failure(ErrorCode::ToolUnavailable,
                               "Asset management is unavailable for this host");
            }
            if (!arguments.contains("path") || !arguments["path"].is_string()) {
                return Failure(ErrorCode::InvalidArguments, "A material path is required");
            }

            std::filesystem::path absolute;
            std::string portable;
            std::string error;
            if (!ResolveMaterialPath(*assets, arguments["path"].get<std::string>(), absolute, portable,
                                     error)) {
                return Failure(ErrorCode::InvalidArguments, error);
            }
            std::error_code existsError;
            if (!std::filesystem::exists(absolute, existsError)) {
                return Failure(ErrorCode::InvalidArguments,
                               "Material not found: " + arguments["path"].get<std::string>());
            }

            auto& registry = resolved->scene->GetRegistry();
            if (!registry.all_of<MeshRendererComponent>(resolved->handle)) {
                return Failure(ErrorCode::ComponentMissing,
                               "Entity does not have a MeshRenderer component to assign material slots");
            }
            auto& renderer = registry.get<MeshRendererComponent>(resolved->handle);
            const int slotCount = std::max(renderer.submeshCount, 1);
            const std::int64_t slot = arguments.value("slot", static_cast<std::int64_t>(0));
            if (slot < 0 || slot >= slotCount) {
                return Failure(ErrorCode::PropertyOutOfRange,
                               "slot " + std::to_string(slot) + " is out of range for " +
                                   std::to_string(slotCount) + " material slot(s)");
            }
            const auto slotIndex = static_cast<std::size_t>(slot);
            if (renderer.appliedMaterialPaths.size() <= slotIndex) {
                renderer.appliedMaterialPaths.resize(slotIndex + 1);
            }
            if (renderer.materialIndices.size() <= slotIndex) {
                renderer.materialIndices.resize(slotIndex + 1, -1);
            }

            ToolResult mutationFailure;
            if (!BeginMutation(context, "Assign Material", mutationFailure)) return mutationFailure;
            renderer.appliedMaterialPaths[slotIndex] = portable;
            renderer.materialIndices[slotIndex] = -1;
            PrefabOverrideSystem::SyncEntityOverrides(
                *resolved->scene, Entity(resolved->handle, resolved->scene));
            context.Backend().CompleteEditorMutation(false);

            Json value = {{"assigned", true},
                          {"material", {{"path", portable}, {"type", "material"}}},
                          {"slot", slot}};
            value["entity"] = context.Entities().Bind(*resolved->scene, resolved->handle).value;
            return ToolResult::Success(std::move(value));
        }, true));
}

} // namespace Hephaestus::Automation
