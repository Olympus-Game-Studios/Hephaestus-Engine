#pragma once

/**
 * @file EditorAssetImportService.h
 * @brief EditorAssetPipeline-backed implementation of the automation import port.
 *
 * Qt-free adapter that lets the content-authoring automation modules queue
 * imports/reimports and poll completion without including the renderer/Vulkan
 * include chain that EditorAssetPipeline transitively pulls in.
 */

#include "Automation/AssetImportService.h"
#include "EditorAssetPipeline.h"

#include <mutex>
#include <string>
#include <unordered_map>

class EditorAssetImportService final : public Hephaestus::Automation::IAssetImportService {
public:
    explicit EditorAssetImportService(EditorAssetPipeline& pipeline) : m_pipeline(pipeline) {}

    bool CanImport(const std::string& absoluteSource, std::string& outTypeName) override;
    bool QueueImport(const std::string& absoluteSource, std::string& outError) override;
    bool QueueReimport(const std::string& absoluteSource, std::string& outError) override;
    bool PollCompletion(const std::string& absoluteSource,
                        Hephaestus::Automation::AssetImportStatus& outStatus) override;

private:
    static std::string NormalizeKey(const std::string& path);
    void Pump();

    EditorAssetPipeline& m_pipeline;
    std::mutex m_mutex;
    std::unordered_map<std::string, Hephaestus::Automation::AssetImportStatus> m_completed;
};
