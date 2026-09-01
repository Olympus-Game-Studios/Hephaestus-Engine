/**
 * @file EditorAssetServices.h
 * @brief Editor-owned AssetReferenceIndex + AssetOperationService + OpenDocumentRegistry.
 */

#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "../../Assets/AssetOperationService.h"
#include "../../Assets/AssetReferenceIndex.h"
#include "../../Assets/OpenDocumentRegistry.h"

#include <filesystem>
#include <mutex>

namespace Hephaestus::QtEditor {

/**
 * @brief Singleton owning non-Qt asset operation services for the Qt editor.
 */
class EditorAssetServices {
public:
    static EditorAssetServices& Instance();

    EditorAssetServices(const EditorAssetServices&) = delete;
    EditorAssetServices& operator=(const EditorAssetServices&) = delete;

    /** Bind project root, wire service pointers, and ScanProject the index. */
    void OnProjectOpened(const std::filesystem::path& projectRoot);

    /** Clear registries and detach project root. */
    void OnProjectClosed();

    AssetReferenceIndex& ReferenceIndex() { return m_index; }
    const AssetReferenceIndex& ReferenceIndex() const { return m_index; }

    AssetOperationService& Operations() { return m_operations; }
    const AssetOperationService& Operations() const { return m_operations; }

    OpenDocumentRegistry& Documents() { return m_documents; }
    const OpenDocumentRegistry& Documents() const { return m_documents; }

    bool HasProject() const;

private:
    EditorAssetServices();

    mutable std::mutex m_mutex;
    bool m_hasProject = false;
    AssetReferenceIndex m_index;
    AssetOperationService m_operations;
    OpenDocumentRegistry m_documents;
};

} // namespace Hephaestus::QtEditor

#endif // HEPHAESTUS_EDITOR_QT_UI
