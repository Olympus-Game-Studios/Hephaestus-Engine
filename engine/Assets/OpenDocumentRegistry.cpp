#include "OpenDocumentRegistry.h"

#include "AssetPathPolicy.h"

#include <algorithm>

namespace Hephaestus {

const char* ToString(DocumentKind kind) {
    switch (kind) {
    case DocumentKind::Scene:
        return "Scene";
    case DocumentKind::Prefab:
        return "Prefab";
    case DocumentKind::Material:
        return "Material";
    case DocumentKind::HUI:
        return "HUI";
    case DocumentKind::Character:
        return "Character";
    }
    return "Unknown";
}

void OpenDocumentRegistry::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_documents.clear();
}

void OpenDocumentRegistry::SetProjectRoot(const std::filesystem::path& projectRoot) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_projectRoot = projectRoot;
}

std::filesystem::path OpenDocumentRegistry::GetProjectRoot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_projectRoot;
}

std::string OpenDocumentRegistry::makeLookupKey(const std::string& path) const {
    const std::string portable = AssetPathPolicy::ToPortableKey(m_projectRoot, path);
    return AssetPathPolicy::NormalizeSeparators(portable.empty() ? path : portable);
}

OpenDocumentInfo OpenDocumentRegistry::makeInfo(DocumentKind kind,
                                                const std::string& absoluteOrPortablePath,
                                                bool dirty) const {
    OpenDocumentInfo info;
    info.kind = kind;
    info.dirty = dirty;
    info.portablePath = AssetPathPolicy::ToPortableKey(m_projectRoot, absoluteOrPortablePath);
    if (info.portablePath.empty()) {
        info.portablePath = AssetPathPolicy::NormalizeSeparators(absoluteOrPortablePath);
    }
    const auto resolved = AssetPathPolicy::ResolvePortableKey(m_projectRoot, info.portablePath);
    if (!resolved.empty()) {
        info.absolutePath = AssetPathPolicy::NormalizeSeparators(resolved.string());
    } else {
        info.absolutePath = AssetPathPolicy::NormalizeSeparators(absoluteOrPortablePath);
    }
    return info;
}

bool OpenDocumentRegistry::Register(DocumentKind kind,
                                    const std::string& absoluteOrPortablePath,
                                    bool dirty) {
    if (absoluteOrPortablePath.empty()) {
        return false;
    }

    OpenDocumentInfo info = makeInfo(kind, absoluteOrPortablePath, dirty);
    const std::string key = makeLookupKey(info.portablePath);

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& existing : m_documents) {
        if (AssetPathPolicy::KeysEqual(makeLookupKey(existing.portablePath), key)) {
            existing = info;
            return true;
        }
    }
    m_documents.push_back(std::move(info));
    return true;
}

bool OpenDocumentRegistry::Unregister(const std::string& absoluteOrPortablePath) {
    if (absoluteOrPortablePath.empty()) {
        return false;
    }
    const std::string key = makeLookupKey(absoluteOrPortablePath);

    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = std::remove_if(m_documents.begin(), m_documents.end(),
                                   [&](const OpenDocumentInfo& doc) {
                                       return AssetPathPolicy::KeysEqual(
                                           makeLookupKey(doc.portablePath), key);
                                   });
    if (it == m_documents.end()) {
        return false;
    }
    m_documents.erase(it, m_documents.end());
    return true;
}

std::optional<OpenDocumentInfo> OpenDocumentRegistry::FindByPath(
    const std::string& absoluteOrPortablePath) const {
    if (absoluteOrPortablePath.empty()) {
        return std::nullopt;
    }
    const std::string key = makeLookupKey(absoluteOrPortablePath);

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& doc : m_documents) {
        if (AssetPathPolicy::KeysEqual(makeLookupKey(doc.portablePath), key) ||
            AssetPathPolicy::KeysEqual(
                AssetPathPolicy::NormalizeSeparators(doc.absolutePath),
                AssetPathPolicy::NormalizeSeparators(absoluteOrPortablePath))) {
            return doc;
        }
    }
    return std::nullopt;
}

bool OpenDocumentRegistry::RetargetPath(const std::string& oldAbsoluteOrPortablePath,
                                        const std::string& newAbsoluteOrPortablePath) {
    if (oldAbsoluteOrPortablePath.empty() || newAbsoluteOrPortablePath.empty()) {
        return false;
    }

    OpenDocumentInfo before;
    OpenDocumentInfo after;
    std::vector<OpenDocumentRetargetCallback> listeners;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const std::string oldKey = makeLookupKey(oldAbsoluteOrPortablePath);
        for (auto& doc : m_documents) {
            if (!AssetPathPolicy::KeysEqual(makeLookupKey(doc.portablePath), oldKey)) {
                continue;
            }
            before = doc;
            after = makeInfo(doc.kind, newAbsoluteOrPortablePath, doc.dirty);
            doc = after;
            listeners.reserve(m_retargetListeners.size());
            for (const auto& listener : m_retargetListeners) {
                if (listener.callback) {
                    listeners.push_back(listener.callback);
                }
            }
            break;
        }
    }

    if (before.portablePath.empty() && before.absolutePath.empty()) {
        return false;
    }
    for (const auto& callback : listeners) {
        callback(before, after);
    }
    return true;
}

bool OpenDocumentRegistry::SetDirty(const std::string& absoluteOrPortablePath, bool dirty) {
    if (absoluteOrPortablePath.empty()) {
        return false;
    }
    const std::string key = makeLookupKey(absoluteOrPortablePath);

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& doc : m_documents) {
        if (AssetPathPolicy::KeysEqual(makeLookupKey(doc.portablePath), key)) {
            doc.dirty = dirty;
            return true;
        }
    }
    return false;
}

std::vector<OpenDocumentInfo> OpenDocumentRegistry::ListAll() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_documents;
}

std::vector<OpenDocumentInfo> OpenDocumentRegistry::ListDirty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<OpenDocumentInfo> dirty;
    for (const auto& doc : m_documents) {
        if (doc.dirty) {
            dirty.push_back(doc);
        }
    }
    return dirty;
}

OpenDocumentListenerId OpenDocumentRegistry::AddSaveListener(OpenDocumentSaveCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const OpenDocumentListenerId id = m_nextListenerId++;
    m_saveListeners.push_back({id, std::move(callback)});
    return id;
}

void OpenDocumentRegistry::RemoveSaveListener(OpenDocumentListenerId id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_saveListeners.erase(std::remove_if(m_saveListeners.begin(), m_saveListeners.end(),
                                         [id](const SaveListener& listener) {
                                             return listener.id == id;
                                         }),
                          m_saveListeners.end());
}

void OpenDocumentRegistry::SetSaveCallback(OpenDocumentSaveCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_saveListeners.clear();
    if (callback) {
        const OpenDocumentListenerId id = m_nextListenerId++;
        m_saveListeners.push_back({id, std::move(callback)});
    }
}

OpenDocumentListenerId OpenDocumentRegistry::AddRetargetListener(OpenDocumentRetargetCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const OpenDocumentListenerId id = m_nextListenerId++;
    m_retargetListeners.push_back({id, std::move(callback)});
    return id;
}

void OpenDocumentRegistry::RemoveRetargetListener(OpenDocumentListenerId id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_retargetListeners.erase(std::remove_if(m_retargetListeners.begin(),
                                             m_retargetListeners.end(),
                                             [id](const RetargetListener& listener) {
                                                 return listener.id == id;
                                             }),
                              m_retargetListeners.end());
}

void OpenDocumentRegistry::SetRetargetNotificationCallback(OpenDocumentRetargetCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_retargetListeners.clear();
    if (callback) {
        const OpenDocumentListenerId id = m_nextListenerId++;
        m_retargetListeners.push_back({id, std::move(callback)});
    }
}

bool OpenDocumentRegistry::RequestSave(const std::string& absoluteOrPortablePath,
                                       std::string* errorOut) {
    OpenDocumentInfo info;
    std::vector<OpenDocumentSaveCallback> listeners;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        listeners.reserve(m_saveListeners.size());
        for (const auto& listener : m_saveListeners) {
            if (listener.callback) {
                listeners.push_back(listener.callback);
            }
        }
        const std::string key = makeLookupKey(absoluteOrPortablePath);
        for (const auto& doc : m_documents) {
            if (AssetPathPolicy::KeysEqual(makeLookupKey(doc.portablePath), key)) {
                info = doc;
                break;
            }
        }
    }

    if (info.portablePath.empty() && info.absolutePath.empty()) {
        if (errorOut) {
            *errorOut = "Document not registered";
        }
        return false;
    }
    if (listeners.empty()) {
        if (errorOut) {
            *errorOut = "No save callback configured";
        }
        return false;
    }
    for (const auto& callback : listeners) {
        std::string localError;
        if (callback(info, errorOut ? errorOut : &localError)) {
            return true;
        }
    }
    return false;
}

} // namespace Hephaestus
