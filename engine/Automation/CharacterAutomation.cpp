#include "CharacterAutomation.h"

#include "AutomationToolSupport.h"

#include "Assets/AssetManager.h"
#include "Assets/CharacterAsset.h"
#include "Assets/CharacterImport.h"
#include "Assets/FbxImportSettings.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Hephaestus::Automation {
namespace {

using namespace Content;

ToolOutcome Failure(ErrorCode code, std::string message)
{
    return ToolResult::Failure(code, std::move(message));
}

Json SubmeshJson(const CharacterSubmeshSlot& slot)
{
    return Json{{"name", slot.name}, {"material", slot.material}, {"meshPart", slot.meshPart}};
}

Json ClipJson(const CharacterClipEntry& clip)
{
    return Json{{"id", clip.id},
                {"displayName", clip.displayName},
                {"folder", clip.folder},
                {"loop", clip.loop},
                {"speed", clip.speed},
                {"startTime", clip.startTime},
                {"endTime", clip.endTime}};
}

Json SocketJson(const CharacterSocket& socket)
{
    return Json{{"name", socket.name},
                {"bone", socket.bone},
                {"position", Json::array({socket.position[0], socket.position[1], socket.position[2]})},
                {"rotation", Json::array({socket.rotation[0], socket.rotation[1], socket.rotation[2]})}};
}

Json ImportSettingsJson(const CharacterImportSettings& settings)
{
    return Json{{"unitScale", settings.unitScale},
                {"preserveAuthoredEditsOnReimport", settings.preserveAuthoredEditsOnReimport},
                {"fbxSpaceConversion", FbxSpaceConversionToString(settings.fbxSpaceConversion)},
                {"animationSampleRate", settings.animationSampleRate},
                {"stripBoneNamespace", settings.stripBoneNamespace}};
}

Json CharacterAssetJson(const CharacterAsset& asset)
{
    Json parts = Json::array();
    for (const CharacterSubmeshSlot& slot : asset.mesh.submeshes) parts.push_back(SubmeshJson(slot));

    Json clips = Json::array();
    for (const CharacterClipEntry& clip : asset.clips) clips.push_back(ClipJson(clip));

    Json sockets = Json::array();
    for (const CharacterSocket& socket : asset.sockets) sockets.push_back(SocketJson(socket));

    Json folders = Json::array();
    for (const std::string& folder : asset.animationFolders) folders.push_back(folder);

    return Json{
        {"name", asset.name},
        {"assetVersion", asset.assetVersion},
        {"schema", CharacterAsset::kSchema},
        {"import", ImportSettingsJson(asset.import)},
        {"mesh", {{"source", asset.mesh.source}, {"parts", std::move(parts)}}},
        {"skeleton", {{"source", asset.skeleton.source},
                      {"skin", asset.skeleton.skin},
                      {"signature", asset.skeleton.signature}}},
        {"clips", std::move(clips)},
        {"animationFolders", std::move(folders)},
        {"sockets", std::move(sockets)},
        {"controller", {{"scriptPath", asset.controller.scriptPath},
                        {"enabled", asset.controller.enabled}}},
        {"defaults", {{"animation", asset.defaults.animation},
                      {"playbackSpeed", asset.defaults.playbackSpeed}}},
    };
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

} // namespace

void RegisterCharacterAutomation(AutomationService& service)
{
    RegisterOrThrow(service, Definition(
        "character.get_info",
        "Inspect a .hpcharacter asset: mesh parts, skeleton, clips, sockets, and import settings.",
        ObjectSchema({{"path", {{"type", "string"}}}}, Json::array({"path"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            Hephaestus::AssetManager* assets = RequireAssets(context, failure);
            if (!assets) return failure;
            if (!arguments.contains("path") || !arguments["path"].is_string()) {
                return Failure(ErrorCode::InvalidArguments, "A character asset path is required");
            }
            std::string error;
            const auto absolute = ResolveAssetPath(assets->GetProjectRoot(), assets->GetAssetRoot(),
                                                   arguments["path"].get<std::string>(), error);
            if (!absolute) return Failure(ErrorCode::InvalidArguments, error);
            std::error_code existsError;
            if (!std::filesystem::exists(*absolute, existsError)) {
                return Failure(ErrorCode::InvalidArguments,
                               "Character asset not found: " +
                                   arguments["path"].get<std::string>());
            }

            CharacterAsset asset;
            if (!CharacterSerializer::Deserialize(asset, *absolute, error)) {
                return Failure(ErrorCode::InvalidArguments,
                               error.empty() ? "Failed to read character asset" : error);
            }

            const std::string portable =
                AssetPathPolicy::ToPortableKey(assets->GetProjectRoot(), absolute->generic_string());
            Json value;
            value["character"] = {{"path", portable},{"type", "character"}};
            value["data"] = CharacterAssetJson(asset);

            // Derive skeleton summary and bone/clip counts from the owned source
            // when it is present, reusing the existing importer inspection path.
            const std::string skeletonSource = asset.skeleton.source.empty()
                ? asset.mesh.source
                : asset.skeleton.source;
            if (!skeletonSource.empty()) {
                const std::filesystem::path sourcePath =
                    AssetPathPolicy::ResolvePortableKey(assets->GetProjectRoot(), skeletonSource);
                std::error_code sourceError;
                if (std::filesystem::exists(sourcePath, sourceError)) {
                    CharacterInspectResult inspect;
                    if (CharacterImporter::Inspect(sourcePath, inspect, error) && inspect.ok) {
                        value["skeletonSummary"] = {
                            {"boneCount", inspect.boneCount},
                            {"clipCount", inspect.clipCount},
                            {"materialSlotCount", inspect.materialSlotCount},
                            {"skinnedMeshCount", inspect.skinnedMeshCount},
                            {"skinnedMeshNames", inspect.skinnedMeshNames},
                            {"skinName", inspect.skinName},
                            {"sourceFormat", inspect.sourceFormat},
                        };
                        if (!inspect.warnings.empty()) value["warnings"] = inspect.warnings;
                    } else {
                        value["skeletonSummary"] = {{"available", false}, {"error", error}};
                    }
                } else {
                    value["skeletonSummary"] = {{"available", false},
                                                {"error", "Owned source is missing"}};
                }
            }
            return ToolResult::Success(std::move(value));
        }));

    RegisterOrThrow(service, Definition(
        "character.import",
        "Import a GLB/FBX source as a .hpcharacter through CharacterImporter.",
        ObjectSchema({{"source", {{"type", "string"}}},
                      {"name", {{"type", "string"}}},
                      {"destination", {{"type", "string"}}},
                      {"createController", {{"type", "boolean"}}},
                      {"settings", {{"type", "object"},
                                    {"properties", {{"unitScale", {{"type", "number"}}},
                                                    {"animationSampleRate", {{"type", "number"}}},
                                                    {"stripBoneNamespace", {{"type", "boolean"}}},
                                                    {"fbxSpaceConversion", {{"type", "string"}}}}}}}},
                     Json::array({"source"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            Hephaestus::AssetManager* assets = RequireAssets(context, failure);
            if (!assets) return failure;
            if (!arguments.contains("source") || !arguments["source"].is_string() ||
                arguments["source"].get<std::string>().empty()) {
                return Failure(ErrorCode::InvalidArguments, "A character source path is required");
            }

            const std::string sourceArgument = arguments["source"].get<std::string>();
            std::filesystem::path sourcePath(sourceArgument);
            if (!sourcePath.is_absolute()) {
                sourcePath = AssetPathPolicy::ResolvePortableKey(assets->GetProjectRoot(), sourceArgument);
            }
            sourcePath = sourcePath.lexically_normal();
            std::error_code existsError;
            if (!std::filesystem::exists(sourcePath, existsError) ||
                !CharacterImporter::IsCharacterSourcePath(sourcePath)) {
                return Failure(ErrorCode::InvalidArguments,
                               "Character source must be an existing GLB or FBX file: " +
                                   sourceArgument);
            }

            std::string error;
            const auto destinationFolder = ResolveAssetFolder(
                assets->GetAssetRoot(), arguments.value("destination", std::string("characters")),
                error);
            if (!destinationFolder) return Failure(ErrorCode::InvalidArguments, error);

            std::string name = arguments.value("name", std::string());
            if (name.empty()) name = sourcePath.stem().string();
            if (name.empty() || name.find('/') != std::string::npos ||
                name.find('\\') != std::string::npos) {
                return Failure(ErrorCode::InvalidArguments,
                               "Character name must be a plain name without path separators");
            }

            CharacterImportSettings importSettings;
            const Json settings = arguments.value("settings", Json::object());
            if (settings.is_object()) {
                if (settings.contains("unitScale") && settings["unitScale"].is_number()) {
                    importSettings.unitScale = settings["unitScale"].get<float>();
                } else if (settings.contains("unitScale")) {
                    return Failure(ErrorCode::InvalidArguments, "settings.unitScale must be a number");
                }
                if (settings.contains("animationSampleRate") &&
                    settings["animationSampleRate"].is_number()) {
                    importSettings.animationSampleRate = settings["animationSampleRate"].get<float>();
                } else if (settings.contains("animationSampleRate")) {
                    return Failure(ErrorCode::InvalidArguments,
                                   "settings.animationSampleRate must be a number");
                }
                if (settings.contains("stripBoneNamespace") &&
                    settings["stripBoneNamespace"].is_boolean()) {
                    importSettings.stripBoneNamespace = settings["stripBoneNamespace"].get<bool>();
                } else if (settings.contains("stripBoneNamespace")) {
                    return Failure(ErrorCode::InvalidArguments,
                                   "settings.stripBoneNamespace must be a boolean");
                }
                if (settings.contains("fbxSpaceConversion") &&
                    settings["fbxSpaceConversion"].is_string()) {
                    importSettings.fbxSpaceConversion = FbxSpaceConversionFromString(
                        settings["fbxSpaceConversion"].get<std::string>());
                } else if (settings.contains("fbxSpaceConversion")) {
                    return Failure(ErrorCode::InvalidArguments,
                                   "settings.fbxSpaceConversion must be a string");
                }
            }

            // Inspect before importing so the caller gets mesh/skeleton metadata
            // and clear diagnostics even when the import itself fails.
            CharacterInspectResult inspect;
            std::string inspectError;
            const bool inspected =
                CharacterImporter::Inspect(sourcePath, CharacterImporter::MakeSettings(importSettings),
                                           inspect, inspectError) &&
                inspect.ok;

            CharacterImportRequest request;
            request.sourceGlb = sourcePath;
            request.projectRoot = assets->GetProjectRoot();
            request.destinationFolderAbs = *destinationFolder;
            request.characterName = name;
            request.createController = arguments.value("createController", true);
            request.settings = CharacterImporter::MakeSettings(importSettings);

            std::string portable;
            if (!CharacterImporter::Import(request, portable, error)) {
                return ToolResult::Failure(ErrorCode::InternalError,
                                           error.empty() ? "Character import failed" : error,
                                           Json{{"warnings", inspect.warnings}});
            }

            assets->Refresh();
            context.Backend().NotifyAssetChanged();

            Json value;
            value["character"] = {{"path", portable}, {"type", "character"}, {"name", name}};
            if (inspected) {
                value["mesh"] = {{"parts", inspect.skinnedMeshNames},
                                 {"skinnedMeshCount", inspect.skinnedMeshCount},
                                 {"materialSlotCount", inspect.materialSlotCount}};
                value["skeleton"] = {{"skin", inspect.skinName},
                                     {"boneCount", inspect.boneCount},
                                     {"sourceFormat", inspect.sourceFormat}};
                value["clipCount"] = inspect.clipCount;
                value["warnings"] = inspect.warnings;
            } else {
                value["inspectError"] = inspectError;
            }
            return ToolResult::Success(std::move(value));
        }, true));

    RegisterOrThrow(service, Definition(
        "character.import_clips",
        "Import animation clips from a GLB/FBX into an existing .hpcharacter, as Import Animation does.",
        ObjectSchema({{"path", {{"type", "string"}}},
                      {"source", {{"type", "string"}}},
                      {"folder", {{"type", "string"}}},
                      {"clips", {{"type", "array"},
                                 {"items", {{"type", "object"},
                                            {"properties", {{"sourceClip", {{"type", "string"}}},
                                                            {"name", {{"type", "string"}}}}}}}}}},
                     Json::array({"path", "source", "clips"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            Hephaestus::AssetManager* assets = RequireAssets(context, failure);
            if (!assets) return failure;
            if (!arguments.contains("path") || !arguments["path"].is_string() ||
                !arguments.contains("source") || !arguments["source"].is_string() ||
                !arguments.contains("clips") || !arguments["clips"].is_array()) {
                return Failure(ErrorCode::InvalidArguments, "path, source and clips are required");
            }
            std::string error;
            const auto absolute = ResolveAssetPath(assets->GetProjectRoot(), assets->GetAssetRoot(),
                                                   arguments["path"].get<std::string>(), error);
            if (!absolute) return Failure(ErrorCode::InvalidArguments, error);
            CharacterAsset asset;
            if (!CharacterSerializer::Deserialize(asset, *absolute, error)) {
                return Failure(ErrorCode::InvalidArguments,
                               error.empty() ? "Failed to read character asset" : error);
            }
            std::filesystem::path sourcePath(arguments["source"].get<std::string>());
            if (!sourcePath.is_absolute()) {
                sourcePath = AssetPathPolicy::ResolvePortableKey(assets->GetProjectRoot(), sourcePath.generic_string());
            }
            std::vector<CharacterClipImportChoice> choices;
            for (const Json& clip : arguments["clips"]) {
                if (!clip.is_object() || !clip.contains("sourceClip") || !clip["sourceClip"].is_string()) {
                    return Failure(ErrorCode::InvalidArguments, "Each clip needs a sourceClip string");
                }
                choices.push_back({clip["sourceClip"].get<std::string>(), clip.value("name", std::string())});
            }
            const std::string portable =
                AssetPathPolicy::ToPortableKey(assets->GetProjectRoot(), absolute->generic_string());
            std::vector<std::string> added;
            if (!CharacterImporter::ImportAnimationClips(assets->GetProjectRoot(), asset, portable,
                                                         sourcePath.lexically_normal(), choices,
                                                         arguments.value("folder", std::string()), added, error) ||
                !CharacterSerializer::Serialize(asset, *absolute, error)) {
                return Failure(ErrorCode::InternalError, error.empty() ? "Clip import failed" : error);
            }
            assets->Refresh();
            context.Backend().NotifyAssetChanged();
            return ToolResult::Success(Json{{"character", portable}, {"added", added}});
        }, true));
}

} // namespace Hephaestus::Automation
