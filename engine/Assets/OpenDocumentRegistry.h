/**
 * @file OpenDocumentRegistry.h
 * @brief Tracks editor-open authored documents (non-Qt).
 *
 * Used by AssetOperationService to retarget open document paths after
 * rename/move and to surface dirty documents before destructive ops.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Hephaestus {

enum class DocumentKind {
    Scene,
    Prefab,
    Material,
    HUI,
    Character,
    Plugin
};

const char* ToString(DocumentKind kind);

struct OpenDocumentInfo {
    DocumentKind kind = DocumentKind::Scene;
    std::string absolutePath;
    std::string portablePath;
    bool dirty = false;
};

using OpenDocumentSaveCallback =
    std::function<bool(const OpenDocumentInfo& doc, std::string* errorOut)>;
using OpenDocumentRetargetCallback =
    std::function<void(const OpenDocumentInfo& before, const OpenDocumentInfo& after)>;
using OpenDocumentListenerId = std::uint64_t;

/**
 * @brief Registry of currently open authored documents.
 */
class OpenDocumentRegistry {
public:
    void Clear();

    void SetProjectRoot(const std::filesystem::path& projectRoot);
    std::filesystem::path GetProjectRoot() const;

    /** Register or refresh an open document. Paths may be absolute or portable. */
    bool Register(DocumentKind kind,
                  const std::string& absoluteOrPortablePath,
                  bool dirty = false);

    /** Unregister by absolute or portable path. Returns true if removed. */
    bool Unregister(const std::string& absoluteOrPortablePath);

    /** Find by absolute or portable path. */
    std::optional<OpenDocumentInfo> FindByPath(const std::string& absoluteOrPortablePath) const;

    /** Update paths after a rename/move. Notifies retarget callback when set. */
    bool RetargetPath(const std::string& oldAbsoluteOrPortablePath,
                      const std::string& newAbsoluteOrPortablePath);

    /** Mark dirty flag for a registered document. */
    bool SetDirty(const std::string& absoluteOrPortablePath, bool dirty);

    std::vector<OpenDocumentInfo> ListAll() const;
    std::vector<OpenDocumentInfo> ListDirty() const;

    /** Replace save listeners with a single callback (test/compat helper). */
    void SetSaveCallback(OpenDocumentSaveCallback callback);
    OpenDocumentListenerId AddSaveListener(OpenDocumentSaveCallback callback);
    void RemoveSaveListener(OpenDocumentListenerId id);

    /** Replace retarget listeners with a single callback (test/compat helper). */
    void SetRetargetNotificationCallback(OpenDocumentRetargetCallback callback);
    OpenDocumentListenerId AddRetargetListener(OpenDocumentRetargetCallback callback);
    void RemoveRetargetListener(OpenDocumentListenerId id);

    /**
     * Invoke save callback for a dirty document if configured.
     * Returns false when callback missing, document missing, or save fails.
     */
    bool RequestSave(const std::string& absoluteOrPortablePath, std::string* errorOut = nullptr);

private:
    std::string makeLookupKey(const std::string& path) const;
    OpenDocumentInfo makeInfo(DocumentKind kind,
                              const std::string& absoluteOrPortablePath,
                              bool dirty) const;

    struct SaveListener {
        OpenDocumentListenerId id = 0;
        OpenDocumentSaveCallback callback;
    };
    struct RetargetListener {
        OpenDocumentListenerId id = 0;
        OpenDocumentRetargetCallback callback;
    };

    mutable std::mutex m_mutex;
    std::filesystem::path m_projectRoot;
    std::vector<OpenDocumentInfo> m_documents;
    std::vector<SaveListener> m_saveListeners;
    std::vector<RetargetListener> m_retargetListeners;
    OpenDocumentListenerId m_nextListenerId = 1;
};

} // namespace Hephaestus
