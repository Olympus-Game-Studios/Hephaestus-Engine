#include "AssetAutomation.h"

#include "AutomationToolSupport.h"

#include "Assets/AssetDatabase.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetOperationService.h"
#include "Assets/AssetReferenceIndex.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Hephaestus::Automation {
namespace {

using namespace Content;

constexpr std::uint64_t kImportTimeoutFrames = 1800; // ~30s at the editor tick rate

ToolOutcome Failure(ErrorCode code, std::string message, Json data = Json::object())
{
    return ToolResult::Failure(code, std::move(message), std::move(data));
}

bool ContainsInsensitive(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) return true;
    return ToLowerAscii(haystack).find(ToLowerAscii(needle)) != std::string::npos;
}

bool MatchesType(AssetType type, const std::string& filter)
{
    if (filter.empty()) return true;
    return ToLowerAscii(ToString(type)) == ToLowerAscii(filter);
}

bool IsUnderBase(const std::string& base, const std::string& relative)
{
    if (base.empty()) return true;
    if (relative == base) return true;
    return relative.size() > base.size() &&
           relative.compare(0, base.size(), base) == 0 &&
           relative[base.size()] == '/';
}

std::string PortableKey(Hephaestus::AssetManager& assets, const std::string& relativePath)
{
    const std::string absolute = assets.ToAbsolutePath(relativePath);
    return AssetPathPolicy::ToPortableKey(assets.GetProjectRoot(), absolute);
}

Json EntryJson(Hephaestus::AssetManager& assets, const std::string& relativePath,
               const std::string& name, AssetType type, bool isDirectory,
               std::uint64_t sizeBytes)
{
    return Json{{"path", PortableKey(assets, relativePath)},
                {"name", name},
                {"type", ToString(type)},
                {"isDirectory", isDirectory},
                {"sizeBytes", sizeBytes}};
}

std::string TrimForName(const std::string& value)
{
    const std::size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const std::size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

bool IsValidFileName(const std::string& name)
{
    if (name.empty()) return false;
    if (name == "." || name == "..") return false;
    return name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}

// ── Deferred import/reimport completion ────────────────────────────────────

class ImportOperation final : public DeferredOperation {
public:
    ImportOperation(IAssetImportService& service, std::string absolutePath, std::string portablePath,
                    bool isImport, std::uint64_t startFrame)
        : m_service(&service),
          m_absolutePath(std::move(absolutePath)),
          m_portablePath(std::move(portablePath)),
          m_isImport(isImport),
          m_startFrame(startFrame)
    {
    }

    std::optional<ToolResult> Poll(ToolContext& context) override
    {
        AssetImportStatus status;
        if (m_service->PollCompletion(m_absolutePath, status)) {
            return BuildResult(status);
        }
        if (context.FrameIndex() - m_startFrame > kImportTimeoutFrames) {
            return ToolResult::Failure(
                ErrorCode::InternalError,
                std::string("Timed out waiting for asset ") + (m_isImport ? "import" : "reimport") +
                    ": " + m_portablePath,
                Json{{"asset", {{"path", m_portablePath}}}, {"timeout", true}});
        }
        return std::nullopt;
    }

private:
    ToolResult BuildResult(const AssetImportStatus& status) const
    {
        const std::string type = status.assetType.empty() ? "unknown" : status.assetType;
        Json value = {{"asset", {{"path", m_portablePath}, {"type", type}}},
                      {"success", status.success},
                      {"message", status.message}};
        if (m_isImport) value["imported"] = status.success;
        if (!status.warnings.empty()) value["warnings"] = status.warnings;
        if (!status.success) {
            return ToolResult::Failure(ErrorCode::InternalError,
                                       status.message.empty() ? "Asset import failed"
                                                              : status.message,
                                       std::move(value));
        }
        return ToolResult::Success(std::move(value));
    }

    IAssetImportService* m_service = nullptr;
    std::string m_absolutePath;
    std::string m_portablePath;
    bool m_isImport = false;
    std::uint64_t m_startFrame = 0;
};

// ── Asset metadata helpers ─────────────────────────────────────────────────

Json AssetInfoJson(Hephaestus::AssetManager& assets, const Hephaestus::AssetInfo& info)
{
    return Json{{"path", PortableKey(assets, info.relativePath)},
                {"name", info.name},
                {"type", ToString(info.type)},
                {"sizeBytes", info.sizeBytes},
                {"lastModifiedTime", info.lastModifiedTime},
                {"isDirectory", info.isDirectory}};
}

void AppendReferences(Json& out, const std::vector<Hephaestus::AssetReference>& references,
                      bool dependencies)
{
    Json array = Json::array();
    constexpr std::size_t kMaxReferences = 200;
    for (std::size_t index = 0; index < references.size() && index < kMaxReferences; ++index) {
        const Hephaestus::AssetReference& reference = references[index];
        array.push_back({{"path", dependencies ? reference.referencedPath : reference.documentPath},
                         {"field", Hephaestus::ToString(reference.fieldKind)},
                         {"location", reference.structuredLocation},
                         {"advisory", reference.isAdvisory}});
    }
    out = std::move(array);
}

struct AssetContext {
    Hephaestus::AssetManager* assets = nullptr;
    std::string projectRoot;
    std::string assetRoot;
};

std::optional<AssetContext> RequireAssets(ToolContext& context, ToolResult& outFailure)
{
    Hephaestus::AssetManager* assets = context.Backend().Assets();
    if (!assets || !assets->HasValidAssetRoot()) {
        outFailure = ToolResult::Failure(ErrorCode::ToolUnavailable,
                                         "Asset management is unavailable for this host");
        return std::nullopt;
    }
    AssetContext result;
    result.assets = assets;
    result.projectRoot = assets->GetProjectRoot();
    result.assetRoot = assets->GetAssetRoot();
    return result;
}

void RefreshAfterAssetChange(ToolContext& context, const AssetContext& assets)
{
    assets.assets->Refresh();
    context.Backend().NotifyAssetChanged();
}

} // namespace

void RegisterAssetAutomation(AutomationService& service)
{
    RegisterOrThrow(service, Definition(
        "asset.list",
        "List project assets. Returns stable project-relative asset keys.",
        ObjectSchema({{"path", {{"type", "string"}}},
                      {"type", {{"type", "string"}}},
                      {"name", {{"type", "string"}}},
                      {"search", {{"type", "string"}}},
                      {"recursive", {{"type", "boolean"}}}}),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto assets = RequireAssets(context, failure);
            if (!assets) return failure;

            std::string error;
            const std::string base = arguments.value("path", std::string());
            const auto baseRelative = ToAssetRelative(assets->projectRoot, assets->assetRoot, base, error);
            if (!baseRelative) return Failure(ErrorCode::InvalidArguments, error);

            const std::string typeFilter = arguments.value("type", std::string());
            const std::string nameFilter = arguments.contains("name")
                ? arguments.value("name", std::string())
                : arguments.value("search", std::string());
            const bool recursive = arguments.value("recursive", false);

            Json entries = Json::array();
            if (recursive) {
                std::vector<Hephaestus::AssetInfo> all = assets->assets->GetAllAssets();
                std::sort(all.begin(), all.end(), [](const auto& left, const auto& right) {
                    return left.relativePath < right.relativePath;
                });
                for (const Hephaestus::AssetInfo& info : all) {
                    if (!IsUnderBase(*baseRelative, info.relativePath)) continue;
                    if (!MatchesType(info.type, typeFilter)) continue;
                    if (!ContainsInsensitive(info.name, nameFilter)) continue;
                    entries.push_back(AssetInfoJson(*assets->assets, info));
                }
            } else {
                std::vector<Hephaestus::DirectoryEntry> listing =
                    assets->assets->ListDirectory(*baseRelative);
                for (const Hephaestus::DirectoryEntry& entry : listing) {
                    if (!MatchesType(entry.type, typeFilter)) continue;
                    if (!ContainsInsensitive(entry.name, nameFilter)) continue;
                    entries.push_back(EntryJson(*assets->assets, entry.relativePath, entry.name,
                                                entry.type, entry.isDirectory, entry.sizeBytes));
                }
            }
            return ToolResult::Success({{"assets", std::move(entries)},
                                        {"path", AssetPathPolicy::ToPortableKey(
                                                     assets->projectRoot,
                                                     assets->assets->ToAbsolutePath(*baseRelative))}});
        }));

    RegisterOrThrow(service, Definition(
        "asset.find",
        "Search the live asset index by name or path.",
        ObjectSchema({{"query", {{"type", "string"}}},
                      {"type", {{"type", "string"}}},
                      {"limit", {{"type", "integer"}, {"minimum", 1}}}},
                     Json::array({"query"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto assets = RequireAssets(context, failure);
            if (!assets) return failure;
            if (!arguments.contains("query") || !arguments["query"].is_string()) {
                return Failure(ErrorCode::InvalidArguments, "A search query is required");
            }
            const std::string query = arguments["query"].get<std::string>();
            const std::string typeFilter = arguments.value("type", std::string());
            const std::int64_t requestedLimit =
                arguments.value("limit", static_cast<std::int64_t>(100));
            const std::size_t limit = requestedLimit < 1
                ? 100
                : std::min<std::size_t>(static_cast<std::size_t>(requestedLimit), 1000);

            std::vector<Hephaestus::AssetInfo> all = assets->assets->GetAllAssets();
            std::sort(all.begin(), all.end(), [](const auto& left, const auto& right) {
                return left.relativePath < right.relativePath;
            });
            Json matches = Json::array();
            std::size_t total = 0;
            for (const Hephaestus::AssetInfo& info : all) {
                if (!MatchesType(info.type, typeFilter)) continue;
                if (!ContainsInsensitive(info.name, query) &&
                    !ContainsInsensitive(info.relativePath, query)) {
                    continue;
                }
                ++total;
                if (matches.size() < limit) matches.push_back(AssetInfoJson(*assets->assets, info));
            }
            return ToolResult::Success({{"matches", std::move(matches)},
                                        {"total", total},
                                        {"query", query}});
        }));

    RegisterOrThrow(service, Definition(
        "asset.get_info",
        "Inspect an asset using the live asset browser, import database, and reference index.",
        ObjectSchema({{"path", {{"type", "string"}}}}, Json::array({"path"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto assets = RequireAssets(context, failure);
            if (!assets) return failure;
            if (!arguments.contains("path") || !arguments["path"].is_string()) {
                return Failure(ErrorCode::InvalidArguments, "An asset path is required");
            }
            std::string error;
            const auto relative = ToAssetRelative(assets->projectRoot, assets->assetRoot,
                                                  arguments["path"].get<std::string>(), error);
            if (!relative) return Failure(ErrorCode::InvalidArguments, error);
            const auto info = assets->assets->GetAssetInfo(*relative);
            if (!info) {
                return ToolResult::Failure(ErrorCode::InvalidArguments,
                                           "Asset not found: " + arguments["path"].get<std::string>(),
                                           Json{{"path", arguments["path"]}});
            }

            Json value;
            value["asset"] = AssetInfoJson(*assets->assets, *info);
            const std::string portable = PortableKey(*assets->assets, info->relativePath);
            value["portableKey"] = portable;
            value["absolutePath"] = info->absolutePath;

            if (AssetDatabase* database = context.Backend().ImportDatabase()) {
                const std::optional<AssetRecord> record =
                    database->GetBySourcePath(info->absolutePath);
                if (record) {
                    value["import"] = {
                        {"assetId", record->assetId},
                        {"importer", record->importerId},
                        {"importerVersion", record->importerVersion},
                        {"importSettingsHash", record->importSettingsHash},
                        {"lastImportTime", record->lastImportTime},
                        {"lastImportSuccess", record->lastImportSuccess},
                        {"lastImportMessage", record->lastImportMessage},
                        {"derivedDataKey", record->derivedDataKey},
                        {"sourceSizeBytes", record->fingerprint.sizeBytes},
                        {"contentHash", record->fingerprint.contentHash},
                        {"imported", record->lastImportSuccess},
                    };
                } else {
                    value["import"] = {{"imported", false}};
                }
            }

            if (Hephaestus::AssetReferenceIndex* references = context.Backend().AssetReferences()) {
                Json dependencies;
                Json referrers;
                AppendReferences(dependencies, references->GetDependencies(portable), true);
                AppendReferences(referrers, references->GetReferrers(portable), false);
                value["dependencies"] = std::move(dependencies);
                value["referrers"] = std::move(referrers);
            }
            return ToolResult::Success(std::move(value));
        }));

    RegisterOrThrow(service, Definition(
        "asset.import",
        "Import an external source file into the project through the editor asset pipeline.",
        ObjectSchema({{"source", {{"type", "string"}}},
                      {"destination", {{"type", "string"}}},
                      {"overwrite", {{"type", "boolean"}}}},
                     Json::array({"source"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto assets = RequireAssets(context, failure);
            if (!assets) return failure;
            if (!arguments.contains("source") || !arguments["source"].is_string() ||
                arguments["source"].get<std::string>().empty()) {
                return Failure(ErrorCode::InvalidArguments, "A source file path is required");
            }

            const std::string sourceArgument = arguments["source"].get<std::string>();
            std::filesystem::path sourcePath(sourceArgument);
            if (!sourcePath.is_absolute()) {
                sourcePath = AssetPathPolicy::ResolvePortableKey(assets->projectRoot, sourceArgument);
            }
            sourcePath = sourcePath.lexically_normal();
            std::error_code existsError;
            if (!std::filesystem::exists(sourcePath, existsError) ||
                !std::filesystem::is_regular_file(sourcePath, existsError)) {
                return Failure(ErrorCode::InvalidArguments,
                               "Source file does not exist: " + sourceArgument);
            }

            std::string error;
            const auto destinationFolder = ResolveAssetFolder(
                assets->assetRoot, arguments.value("destination", std::string()), error);
            if (!destinationFolder) return Failure(ErrorCode::InvalidArguments, error);

            const std::filesystem::path destination =
                (*destinationFolder / sourcePath.filename()).lexically_normal();
            const bool overwrite = arguments.value("overwrite", false);
            std::error_code destinationError;
            if (std::filesystem::exists(destination, destinationError) && !overwrite) {
                return Failure(ErrorCode::InvalidArguments,
                               "Destination already exists: " + destination.generic_string());
            }

            std::error_code createError;
            std::filesystem::create_directories(*destinationFolder, createError);
            if (createError) {
                return Failure(ErrorCode::InternalError,
                               "Failed to create destination folder: " + createError.message());
            }
            std::error_code copyError;
            std::filesystem::copy_file(sourcePath, destination,
                                       std::filesystem::copy_options::overwrite_existing, copyError);
            if (copyError) {
                return Failure(ErrorCode::InternalError,
                               "Failed to copy source into the project: " + copyError.message());
            }

            assets->assets->Refresh();
            const std::string portable =
                AssetPathPolicy::ToPortableKey(assets->projectRoot, destination.generic_string());
            const std::string absolute = destination.generic_string();

            IAssetImportService* importer = context.Backend().AssetImport();
            std::string detectedType;
            if (!importer || !importer->CanImport(absolute, detectedType)) {
                context.Backend().NotifyAssetChanged();
                return ToolResult::Success(
                    {{"asset", {{"path", portable},
                                {"type", ToString(AssetTypeFromExtension(
                                              destination.extension().string()))}}},
                     {"imported", false},
                     {"message", "File copied; the asset type has no importer"}});
            }
            if (!importer->QueueImport(absolute, error)) {
                return Failure(ErrorCode::InternalError,
                               error.empty() ? "Failed to queue asset import" : error);
            }
            return ToolOutcome{std::make_unique<ImportOperation>(*importer, absolute, portable, true,
                                                                 context.FrameIndex())};
        }, true));

    RegisterOrThrow(service, Definition(
        "asset.reimport",
        "Reimport a project asset through the editor reimport pipeline.",
        ObjectSchema({{"path", {{"type", "string"}}}}, Json::array({"path"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto assets = RequireAssets(context, failure);
            if (!assets) return failure;
            IAssetImportService* importer = context.Backend().AssetImport();
            if (!importer) {
                return Failure(ErrorCode::ToolUnavailable,
                               "Asset reimport is unavailable for this host");
            }
            if (!arguments.contains("path") || !arguments["path"].is_string()) {
                return Failure(ErrorCode::InvalidArguments, "An asset path is required");
            }
            std::string error;
            const auto absolute = ResolveAssetPath(assets->projectRoot, assets->assetRoot,
                                                   arguments["path"].get<std::string>(), error);
            if (!absolute) return Failure(ErrorCode::InvalidArguments, error);
            std::error_code existsError;
            if (!std::filesystem::exists(*absolute, existsError)) {
                return Failure(ErrorCode::InvalidArguments,
                               "Asset does not exist: " + arguments["path"].get<std::string>());
            }
            const std::string absoluteString = absolute->generic_string();
            if (!importer->QueueReimport(absoluteString, error)) {
                return Failure(ErrorCode::InternalError,
                               error.empty() ? "Failed to queue asset reimport" : error);
            }
            const std::string portable =
                AssetPathPolicy::ToPortableKey(assets->projectRoot, absoluteString);
            return ToolOutcome{std::make_unique<ImportOperation>(*importer, absoluteString, portable,
                                                                 false, context.FrameIndex())};
        }, true));

    RegisterOrThrow(service, Definition(
        "asset.rename",
        "Rename a project asset through AssetOperationService (reference rewrite + transaction).",
        ObjectSchema({{"path", {{"type", "string"}}},
                      {"newName", {{"type", "string"}}}},
                     Json::array({"path", "newName"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto assets = RequireAssets(context, failure);
            if (!assets) return failure;
            Hephaestus::AssetOperationService* operations = context.Backend().AssetOperations();
            if (!operations) {
                return Failure(ErrorCode::ToolUnavailable,
                               "Asset operations are unavailable for this host");
            }
            if (!arguments.contains("path") || !arguments["path"].is_string() ||
                !arguments.contains("newName") || !arguments["newName"].is_string()) {
                return Failure(ErrorCode::InvalidArguments, "path and newName are required");
            }
            const std::string newName = TrimForName(arguments["newName"].get<std::string>());
            if (!IsValidFileName(newName)) {
                return Failure(ErrorCode::InvalidArguments,
                               "newName must be a plain file name without path separators");
            }
            std::string error;
            const auto absolute = ResolveAssetPath(assets->projectRoot, assets->assetRoot,
                                                   arguments["path"].get<std::string>(), error);
            if (!absolute || !std::filesystem::exists(*absolute)) {
                return Failure(ErrorCode::InvalidArguments,
                               "Asset does not exist: " + arguments["path"].get<std::string>());
            }
            const std::string oldPortable =
                AssetPathPolicy::ToPortableKey(assets->projectRoot, absolute->generic_string());
            const std::filesystem::path newAbsolute = absolute->parent_path() / newName;
            const std::string newPortable =
                AssetPathPolicy::ToPortableKey(assets->projectRoot, newAbsolute.generic_string());

            const AssetOperationResult result = operations->RenameAsset(oldPortable, newPortable);
            if (!result.success) {
                return Failure(ErrorCode::InternalError,
                               result.error.empty() ? "Rename failed" : result.error,
                               result.warnings.empty() ? Json::object()
                                                       : Json{{"warnings", result.warnings}});
            }
            RefreshAfterAssetChange(context, *assets);
            Json value = {{"renamed", true},
                          {"oldPath", oldPortable},
                          {"newPath", newPortable},
                          {"opId", result.opId}};
            if (!result.warnings.empty()) value["warnings"] = result.warnings;
            return ToolResult::Success(std::move(value));
        }, true));

    RegisterOrThrow(service, Definition(
        "asset.move",
        "Move a project asset into another project folder via AssetOperationService.",
        ObjectSchema({{"path", {{"type", "string"}}},
                      {"destination", {{"type", "string"}}}},
                     Json::array({"path", "destination"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto assets = RequireAssets(context, failure);
            if (!assets) return failure;
            Hephaestus::AssetOperationService* operations = context.Backend().AssetOperations();
            if (!operations) {
                return Failure(ErrorCode::ToolUnavailable,
                               "Asset operations are unavailable for this host");
            }
            if (!arguments.contains("path") || !arguments["path"].is_string() ||
                !arguments.contains("destination") || !arguments["destination"].is_string()) {
                return Failure(ErrorCode::InvalidArguments, "path and destination are required");
            }
            std::string error;
            const auto absolute = ResolveAssetPath(assets->projectRoot, assets->assetRoot,
                                                   arguments["path"].get<std::string>(), error);
            if (!absolute || !std::filesystem::exists(*absolute)) {
                return Failure(ErrorCode::InvalidArguments,
                               "Asset does not exist: " + arguments["path"].get<std::string>());
            }
            const auto destinationFolder = ResolveAssetFolder(
                assets->assetRoot, arguments["destination"].get<std::string>(), error);
            if (!destinationFolder) return Failure(ErrorCode::InvalidArguments, error);

            const std::string oldPortable =
                AssetPathPolicy::ToPortableKey(assets->projectRoot, absolute->generic_string());
            const std::filesystem::path newAbsolute =
                (*destinationFolder / absolute->filename()).lexically_normal();
            const std::string newPortable =
                AssetPathPolicy::ToPortableKey(assets->projectRoot, newAbsolute.generic_string());

            const AssetOperationResult result = operations->MoveAsset(oldPortable, newPortable);
            if (!result.success) {
                return Failure(ErrorCode::InternalError,
                               result.error.empty() ? "Move failed" : result.error,
                               result.warnings.empty() ? Json::object()
                                                       : Json{{"warnings", result.warnings}});
            }
            RefreshAfterAssetChange(context, *assets);
            Json value = {{"moved", true},
                          {"oldPath", oldPortable},
                          {"newPath", newPortable},
                          {"opId", result.opId}};
            if (!result.warnings.empty()) value["warnings"] = result.warnings;
            return ToolResult::Success(std::move(value));
        }, true));

    RegisterOrThrow(service, Definition(
        "asset.trash",
        "Move a project asset to the project trash via AssetOperationService.",
        ObjectSchema({{"path", {{"type", "string"}}}}, Json::array({"path"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto assets = RequireAssets(context, failure);
            if (!assets) return failure;
            Hephaestus::AssetOperationService* operations = context.Backend().AssetOperations();
            if (!operations) {
                return Failure(ErrorCode::ToolUnavailable,
                               "Asset operations are unavailable for this host");
            }
            if (!arguments.contains("path") || !arguments["path"].is_string()) {
                return Failure(ErrorCode::InvalidArguments, "An asset path is required");
            }
            std::string error;
            const auto absolute = ResolveAssetPath(assets->projectRoot, assets->assetRoot,
                                                   arguments["path"].get<std::string>(), error);
            if (!absolute || !std::filesystem::exists(*absolute)) {
                return Failure(ErrorCode::InvalidArguments,
                               "Asset does not exist: " + arguments["path"].get<std::string>());
            }
            const std::string portable =
                AssetPathPolicy::ToPortableKey(assets->projectRoot, absolute->generic_string());
            const AssetOperationResult result = operations->MoveToTrash(portable);
            if (!result.success) {
                return Failure(ErrorCode::InternalError,
                               result.error.empty() ? "Trash failed" : result.error);
            }
            RefreshAfterAssetChange(context, *assets);
            return ToolResult::Success({{"trashed", true},
                                        {"path", portable},
                                        {"opId", result.opId},
                                        {"warnings", result.warnings}});
        }, true));

    RegisterOrThrow(service, Definition(
        "asset.restore",
        "Restore a previously trashed asset by operation id.",
        ObjectSchema({{"opId", {{"type", "string"}}}}, Json::array({"opId"})),
        [](ToolContext& context, const Json& arguments) -> ToolOutcome {
            ToolResult failure;
            const auto assets = RequireAssets(context, failure);
            if (!assets) return failure;
            Hephaestus::AssetOperationService* operations = context.Backend().AssetOperations();
            if (!operations) {
                return Failure(ErrorCode::ToolUnavailable,
                               "Asset operations are unavailable for this host");
            }
            if (!arguments.contains("opId") || !arguments["opId"].is_string() ||
                arguments["opId"].get<std::string>().empty()) {
                return Failure(ErrorCode::InvalidArguments, "A trash operation id is required");
            }
            const std::string opId = arguments["opId"].get<std::string>();
            const AssetOperationResult result = operations->RestoreFromTrash(opId);
            if (!result.success) {
                return Failure(ErrorCode::InternalError,
                               result.error.empty() ? "Restore failed" : result.error);
            }
            RefreshAfterAssetChange(context, *assets);
            return ToolResult::Success(
                {{"restored", true}, {"opId", opId}, {"warnings", result.warnings}});
        }, true));
}

} // namespace Hephaestus::Automation
