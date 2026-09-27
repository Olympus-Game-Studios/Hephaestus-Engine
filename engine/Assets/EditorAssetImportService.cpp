#include "EditorAssetImportService.h"

#include "AssetTypes.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>
#include <vector>

std::string EditorAssetImportService::NormalizeKey(const std::string& path)
{
    std::string normalized =
        std::filesystem::path(path).lexically_normal().generic_string();
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return normalized;
}

bool EditorAssetImportService::CanImport(const std::string& absoluteSource, std::string& outTypeName)
{
    const AssetType type = AssetTypeFromExtension(
        std::filesystem::path(absoluteSource).extension().string());
    if (type != AssetType::Texture && type != AssetType::StaticMesh && type != AssetType::Audio) {
        return false;
    }
    outTypeName = ToString(type);
    return true;
}

bool EditorAssetImportService::QueueImport(const std::string& absoluteSource, std::string& outError)
{
    m_pipeline.RegisterCopiedAsset(std::filesystem::path(absoluteSource));
    const ReimportResult queued =
        m_pipeline.RequestReimportByPath(std::filesystem::path(absoluteSource));
    if (!queued.success) {
        outError = queued.message.empty() ? "Failed to queue asset import" : queued.message;
        return false;
    }
    outError.clear();
    return true;
}

bool EditorAssetImportService::QueueReimport(const std::string& absoluteSource, std::string& outError)
{
    const ReimportResult queued =
        m_pipeline.RequestReimportByPath(std::filesystem::path(absoluteSource));
    if (!queued.success) {
        outError = queued.message.empty() ? "Failed to queue asset reimport" : queued.message;
        return false;
    }
    outError.clear();
    return true;
}

bool EditorAssetImportService::PollCompletion(const std::string& absoluteSource,
                                              Hephaestus::Automation::AssetImportStatus& outStatus)
{
    Pump();
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_completed.find(NormalizeKey(absoluteSource));
    if (found == m_completed.end()) return false;
    outStatus = std::move(found->second);
    m_completed.erase(found);
    return true;
}

void EditorAssetImportService::Pump()
{
    std::vector<CompletedReimport> completed = m_pipeline.DrainCompleted();
    if (completed.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (CompletedReimport& entry : completed) {
        Hephaestus::Automation::AssetImportStatus status;
        status.success = entry.result.success;
        status.assetType = ToString(entry.assetType);
        status.message = entry.result.message;
        status.warnings = entry.result.warnings;
        m_completed[NormalizeKey(entry.sourcePath.generic_string())] = std::move(status);
    }
}
