/**
 * @file EditorAssetServices.cpp
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorAssetServices.h"

namespace Hephaestus::QtEditor {

EditorAssetServices& EditorAssetServices::Instance() {
    static EditorAssetServices instance;
    return instance;
}

EditorAssetServices::EditorAssetServices() {
    m_operations.SetReferenceIndex(&m_index);
    m_operations.SetDocumentRegistry(&m_documents);
}

void EditorAssetServices::OnProjectOpened(const std::filesystem::path& projectRoot) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hasProject = !projectRoot.empty();
    m_documents.Clear();
    m_documents.SetProjectRoot(projectRoot);
    m_operations.SetProjectRoot(projectRoot);
    m_index.Clear();
    if (!projectRoot.empty()) {
        m_index.ScanProject(projectRoot);
    }
}

void EditorAssetServices::OnProjectClosed() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hasProject = false;
    m_documents.Clear();
    m_documents.SetProjectRoot({});
    m_operations.SetProjectRoot({});
    m_index.Clear();
}

bool EditorAssetServices::HasProject() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_hasProject;
}

} // namespace Hephaestus::QtEditor

#endif // HEPHAESTUS_EDITOR_QT_UI
