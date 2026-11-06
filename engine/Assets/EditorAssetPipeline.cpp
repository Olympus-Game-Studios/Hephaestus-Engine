#include "EditorAssetPipeline.h"

#include "AssetImporters.h"
#include "AssetTypes.h"

EditorAssetPipeline::~EditorAssetPipeline() {
    Shutdown();
}

bool EditorAssetPipeline::Initialize(const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) {
        return false;
    }

    Shutdown();

    reimport_ = std::make_unique<ReimportManager>(db_, ddc_);
    registerDefaultImporters();

    reimport_->SetProjectRoot(projectRoot);
    reimport_->StartAutoReimport();
    initialized_ = true;
    return true;
}

void EditorAssetPipeline::Shutdown() {
    if (!initialized_ && !reimport_) {
        return;
    }
    reimport_.reset();
    initialized_ = false;
}

void EditorAssetPipeline::RegisterCopiedAsset(const std::filesystem::path& path) {
    if (!initialized_ || !reimport_ || path.empty()) {
        return;
    }

    AssetType type = AssetTypeFromExtension(path.extension().string());
    if (type != AssetType::Texture && type != AssetType::StaticMesh && type != AssetType::Audio) {
        return;
    }

    reimport_->RegisterSourceIfMissing(path, type);
}

ReimportResult EditorAssetPipeline::RequestReimportByPath(const std::filesystem::path& sourcePath,
                                                          ReimportPolicy policy) {
    if (!initialized_ || !reimport_) {
        return {false, "Asset pipeline not initialized", {}, {}};
    }
    return reimport_->RequestReimportByPath(sourcePath, policy);
}

ReimportResult EditorAssetPipeline::ReimportAllChanged() {
    if (!initialized_ || !reimport_) {
        return {false, "Asset pipeline not initialized", {}, {}};
    }
    return reimport_->ReimportAllChanged();
}

std::vector<CompletedReimport> EditorAssetPipeline::DrainCompleted() {
    if (!initialized_ || !reimport_) {
        return {};
    }
    return reimport_->DrainCompleted();
}

void EditorAssetPipeline::registerDefaultImporters() {
    if (!reimport_) {
        return;
    }
    reimport_->RegisterImporter(std::make_unique<TextureImporter>());
    reimport_->RegisterImporter(std::make_unique<StaticMeshImporter>());
    reimport_->RegisterImporter(std::make_unique<AudioImporter>());
}
