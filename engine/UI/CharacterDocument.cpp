#include "CharacterDocument.h"

#include "../Assets/AssetPathPolicy.h"
#include "../Assets/CharacterImport.h"
#include "../Animation/AnimationClip.h"
#include "../Animation/CharacterPlayback.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include <algorithm>
#include <cmath>

namespace Hephaestus {
namespace {

bool ClipIdExists(const CharacterAsset& asset, const std::string& id) {
    for (const CharacterClipEntry& clip : asset.clips) {
        if (clip.id == id) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string CharacterDocument::MakeUniqueClipId(const CharacterAsset& asset, const std::string& desired) {
    std::string base = desired.empty() ? "Clip" : desired;
    if (!ClipIdExists(asset, base)) {
        return base;
    }
    for (int i = 2; i < 10000; ++i) {
        const std::string candidate = base + "_" + std::to_string(i);
        if (!ClipIdExists(asset, candidate)) {
            return candidate;
        }
    }
    return base + "_copy";
}

CharacterDocument::CharacterDocument(Scene* previewScene) : scene_(previewScene) {
    displayName_ = "Character";
}

bool CharacterDocument::HasMeshSource() const {
    return !asset_.mesh.source.empty() || !asset_.skeleton.source.empty();
}

Entity CharacterDocument::FindPreviewCharacter() const {
    if (!scene_) {
        return {};
    }
    auto view = scene_->GetEntitiesWith<CharacterComponent>();
    for (auto handle : view) {
        Entity entity(handle, scene_);
        if (entity.HasComponent<TagComponent>() &&
            entity.GetComponent<TagComponent>().tag == "Character Preview") {
            return entity;
        }
    }
    if (!view.empty()) {
        return Entity(*view.begin(), scene_);
    }
    return {};
}

Entity CharacterDocument::FindSocketGizmo() const {
    if (!scene_) {
        return {};
    }
    auto view = scene_->GetEntitiesWith<TagComponent>();
    for (auto handle : view) {
        Entity entity(handle, scene_);
        if (entity.GetComponent<TagComponent>().tag == "Socket Gizmo") {
            return entity;
        }
    }
    return {};
}

bool CharacterDocument::OnOpen(const std::filesystem::path& path, std::string& outError) {
    path_ = path;
    lastError_.clear();
    if (!CharacterSerializer::Deserialize(asset_, path, outError)) {
        lastError_ = outError;
        return false;
    }
    displayName_ = asset_.name.empty() ? path.filename().string() : asset_.name;
    if (!projectRoot_.empty()) {
        portablePath_ = AssetPathPolicy::ToPortableKey(projectRoot_, path.generic_string());
    }
    dirty_ = false;
    ClearUndo();
    rebuildPreviewWorld();
    return true;
}

bool CharacterDocument::OnSave(std::string& outError) {
    if (path_.empty()) {
        outError = "Character path is empty.";
        return false;
    }
    if (!CharacterSerializer::Serialize(asset_, path_, outError)) {
        lastError_ = outError;
        return false;
    }
    if (!portablePath_.empty()) {
        CharacterRuntime::Invalidate(portablePath_);
    }
    dirty_ = false;
    return true;
}

void CharacterDocument::RebuildPreview() {
    rebuildPreviewWorld();
}

void CharacterDocument::rebuildPreviewWorld() {
    if (!scene_) {
        return;
    }
    scene_->Clear();
    scene_->ClearSelection();

    Entity env = scene_->CreateEntity("Character Preview Environment");
    env.AddComponent<EnvironmentComponent>();
    env.AddComponent<EditorOnlyComponent>();

    Entity light = scene_->CreateEntity("Character Preview Light");
    light.AddComponent<LightComponent>(LightComponent::CreateDirectional(Vec3(0.0f, -1.0f, -0.2f)));
    light.AddComponent<EditorOnlyComponent>();

    Entity preview = scene_->CreateEntity("Character Preview");
    auto& character = preview.AddComponent<CharacterComponent>();
    character.characterPath = portablePath_;
    character.suppressAutoPlay = true;

    Entity gizmo = scene_->CreateEntity("Socket Gizmo");
    gizmo.AddComponent<EditorOnlyComponent>();
}

bool CharacterDocument::BindPreview(EngineFrameRenderer* renderer, bool applyAssetDefaults,
                                    bool forceGpuReload, std::string& outError) {
    outError.clear();
    Entity preview = FindPreviewCharacter();
    if (!preview) {
        outError = "Preview character entity is missing.";
        return false;
    }
    if (projectRoot_.empty() || portablePath_.empty() || !HasMeshSource()) {
        auto& character = preview.GetComponent<CharacterComponent>();
        character.runtimeReady = false;
        character.lastError = HasMeshSource() ? "Character path is not resolved."
                                              : "Character has no mesh source.";
        lastError_ = character.lastError;
        return false;
    }

    auto& character = preview.GetComponent<CharacterComponent>();
    character.characterPath = portablePath_;
    character.suppressAutoPlay = true;
    if (!CharacterRuntime::BindEntity(preview, projectRoot_, applyAssetDefaults, outError)) {
        lastError_ = outError;
        return false;
    }
    character.suppressAutoPlay = true;

    CharacterRuntimeData data;
    if (!CharacterRuntime::Load(projectRoot_, portablePath_, data, outError)) {
        lastError_ = outError;
        return false;
    }
    previewMeshClips_ = std::move(data.imported.clips);
    previewExtraClipSources_.clear();
    SyncPreviewClipMetadata();

    if (!CharacterRuntime::UploadVisual(preview, projectRoot_, renderer, outError, forceGpuReload)) {
        lastError_ = outError;
        return false;
    }
    lastError_.clear();
    return true;
}

void CharacterDocument::SyncPreviewClipMetadata() {
    Entity preview = FindPreviewCharacter();
    if (!preview || !preview.HasComponent<CharacterComponent>()) {
        return;
    }
    auto& character = preview.GetComponent<CharacterComponent>();
    if (!character.runtimeReady) {
        return;
    }
    std::vector<std::string> warnings;
    character.clips = CharacterRuntime::BindAuthoredClips(
        asset_, projectRoot_, CharacterImporter::MakeSettings(asset_.import), character.skeleton,
        previewMeshClips_, previewExtraClipSources_, warnings);
}

void CharacterDocument::BeginAuthoredEdit() {
    PushUndoSnapshot();
}

void CharacterDocument::MarkAuthoredChanged() {
    SetDirty(true);
    SyncPreviewClipMetadata();
}

CharacterClipEntry* CharacterDocument::FindClipById(const std::string& clipId) {
    for (CharacterClipEntry& clip : asset_.clips) {
        if (clip.id == clipId) {
            return &clip;
        }
    }
    return nullptr;
}

const CharacterClipEntry* CharacterDocument::FindClipById(const std::string& clipId) const {
    for (const CharacterClipEntry& clip : asset_.clips) {
        if (clip.id == clipId) {
            return &clip;
        }
    }
    return nullptr;
}

CharacterSocket* CharacterDocument::FindSocketByName(const std::string& name) {
    for (CharacterSocket& socket : asset_.sockets) {
        if (socket.name == name) {
            return &socket;
        }
    }
    return nullptr;
}

const CharacterSocket* CharacterDocument::FindSocketByName(const std::string& name) const {
    for (const CharacterSocket& socket : asset_.sockets) {
        if (socket.name == name) {
            return &socket;
        }
    }
    return nullptr;
}

float CharacterDocument::sourceDurationForClip(const CharacterClipEntry& clip) const {
    if (clip.endTime > clip.startTime) {
        return std::max(clip.endTime, clip.startTime);
    }
    return std::max(0.0f, clip.endTime);
}

bool CharacterDocument::validateClipTrim(const CharacterClipEntry& clip, float startTime, float endTime,
                                         std::string& outError) const {
    if (!(startTime < endTime)) {
        outError = "Trim start must be less than trim end.";
        return false;
    }
    if (startTime < 0.0f) {
        outError = "Trim start must be >= 0.";
        return false;
    }
    const float sourceEnd = clip.endTime > clip.startTime ? std::max(clip.endTime, endTime) : endTime;
    if (endTime > sourceEnd + 1e-4f && clip.endTime > 0.0f && endTime > clip.endTime + 1e-4f &&
        startTime > 0.0f && endTime > sourceDurationForClip(clip) + 1.0f) {
        outError = "Trim range is outside the source clip duration.";
        return false;
    }
    return true;
}

bool CharacterDocument::SetClipDisplayName(const std::string& clipId, const std::string& displayName,
                                           std::string& outError) {
    CharacterClipEntry* clip = FindClipById(clipId);
    if (!clip) {
        outError = "Clip not found.";
        return false;
    }
    if (displayName.empty()) {
        outError = "Display name cannot be empty.";
        return false;
    }
    BeginAuthoredEdit();
    clip->displayName = displayName;
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::RenameClip(const std::string& clipId, const std::string& newName,
                                   std::string& outNewId, std::string& outError) {
    outNewId.clear();
    CharacterClipEntry* clip = FindClipById(clipId);
    if (!clip) {
        outError = "Clip not found.";
        return false;
    }
    std::string trimmed = newName;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
        trimmed.erase(trimmed.begin());
    }
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) {
        trimmed.pop_back();
    }
    if (trimmed.empty()) {
        outError = "Clip name cannot be empty.";
        return false;
    }
    if (trimmed == clip->id) {
        outNewId = clip->id;
        if (trimmed != clip->displayName) {
            BeginAuthoredEdit();
            clip->displayName = trimmed;
            MarkAuthoredChanged();
        }
        return true;
    }
    if (trimmed != clip->id && ClipIdExists(asset_, trimmed)) {
        outError = "A clip named '" + trimmed + "' already exists.";
        return false;
    }
    const std::string oldId = clip->id;
    BeginAuthoredEdit();
    clip->id = trimmed;
    clip->displayName = trimmed;
    if (asset_.defaults.animation == oldId) {
        asset_.defaults.animation = trimmed;
    }
    Entity preview = FindPreviewCharacter();
    if (preview && preview.HasComponent<CharacterComponent>()) {
        auto& character = preview.GetComponent<CharacterComponent>();
        if (character.defaultAnimation == oldId) {
            character.defaultAnimation = trimmed;
        }
        if (character.player.currentClip == oldId) {
            character.player.currentClip = trimmed;
        }
        if (character.player.pendingClip == oldId) {
            character.player.pendingClip = trimmed;
        }
        if (character.player.fadeFromClip == oldId) {
            character.player.fadeFromClip = trimmed;
        }
        if (character.player.pendingFadeClip == oldId) {
            character.player.pendingFadeClip = trimmed;
        }
        if (character.player.finishedClip == oldId) {
            character.player.finishedClip = trimmed;
        }
        for (AnimationClip& runtimeClip : character.clips) {
            if (runtimeClip.id == oldId) {
                runtimeClip.id = trimmed;
            }
        }
    }
    MarkAuthoredChanged();
    outNewId = trimmed;
    return true;
}

bool CharacterDocument::SetClipFolder(const std::string& clipId, const std::string& folder,
                                      std::string& outError) {
    CharacterClipEntry* clip = FindClipById(clipId);
    if (!clip) {
        outError = "Clip not found.";
        return false;
    }
    BeginAuthoredEdit();
    clip->folder = folder;
    if (!folder.empty()) {
        bool known = false;
        for (const std::string& existing : asset_.animationFolders) {
            if (existing == folder) {
                known = true;
                break;
            }
        }
        if (!known) {
            asset_.animationFolders.push_back(folder);
        }
    }
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::SetClipLoop(const std::string& clipId, bool loop, std::string& outError) {
    CharacterClipEntry* clip = FindClipById(clipId);
    if (!clip) {
        outError = "Clip not found.";
        return false;
    }
    BeginAuthoredEdit();
    clip->loop = loop;
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::SetClipSpeed(const std::string& clipId, float speed, std::string& outError) {
    CharacterClipEntry* clip = FindClipById(clipId);
    if (!clip) {
        outError = "Clip not found.";
        return false;
    }
    if (!(speed > 0.0f)) {
        outError = "Playback speed must be greater than 0.";
        return false;
    }
    BeginAuthoredEdit();
    clip->speed = speed;
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::SetClipTrim(const std::string& clipId, float startTime, float endTime,
                                    std::string& outError) {
    CharacterClipEntry* clip = FindClipById(clipId);
    if (!clip) {
        outError = "Clip not found.";
        return false;
    }
    if (!(startTime < endTime)) {
        outError = "Trim start must be less than trim end.";
        return false;
    }
    if (startTime < 0.0f) {
        outError = "Trim start must be >= 0.";
        return false;
    }
    BeginAuthoredEdit();
    clip->startTime = startTime;
    clip->endTime = endTime;
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::DuplicateClip(const std::string& clipId, std::string& outNewId,
                                      std::string& outError) {
    const CharacterClipEntry* source = FindClipById(clipId);
    if (!source) {
        outError = "Clip not found.";
        return false;
    }
    BeginAuthoredEdit();
    CharacterClipEntry copy = *source;
    copy.id = MakeUniqueClipId(asset_, source->id + "_Copy");
    copy.displayName = source->displayName.empty() ? copy.id : source->displayName + " Copy";
    outNewId = copy.id;
    asset_.clips.push_back(std::move(copy));
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::DeleteClip(const std::string& clipId, std::string& outError) {
    auto it = std::find_if(asset_.clips.begin(), asset_.clips.end(),
                           [&](const CharacterClipEntry& clip) { return clip.id == clipId; });
    if (it == asset_.clips.end()) {
        outError = "Clip not found.";
        return false;
    }
    BeginAuthoredEdit();
    if (asset_.defaults.animation == clipId) {
        asset_.defaults.animation.clear();
    }
    asset_.clips.erase(it);
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::AddAnimationFolder(const std::string& folder, std::string& outError) {
    if (folder.empty()) {
        outError = "Folder name cannot be empty.";
        return false;
    }
    for (const std::string& existing : asset_.animationFolders) {
        if (existing == folder) {
            outError = "Folder already exists.";
            return false;
        }
    }
    BeginAuthoredEdit();
    asset_.animationFolders.push_back(folder);
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::RenameAnimationFolder(const std::string& oldFolder, const std::string& newFolder,
                                              std::string& outError) {
    if (newFolder.empty()) {
        outError = "Folder name cannot be empty.";
        return false;
    }
    auto it = std::find(asset_.animationFolders.begin(), asset_.animationFolders.end(), oldFolder);
    if (it == asset_.animationFolders.end()) {
        outError = "Folder not found.";
        return false;
    }
    BeginAuthoredEdit();
    *it = newFolder;
    for (CharacterClipEntry& clip : asset_.clips) {
        if (clip.folder == oldFolder) {
            clip.folder = newFolder;
        }
    }
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::DeleteEmptyAnimationFolder(const std::string& folder, std::string& outError) {
    for (const CharacterClipEntry& clip : asset_.clips) {
        if (clip.folder == folder) {
            outError = "Folder is not empty.";
            return false;
        }
    }
    auto it = std::find(asset_.animationFolders.begin(), asset_.animationFolders.end(), folder);
    if (it == asset_.animationFolders.end()) {
        outError = "Folder not found.";
        return false;
    }
    BeginAuthoredEdit();
    asset_.animationFolders.erase(it);
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::CreateSocket(const std::string& name, const std::string& bone,
                                     std::string& outError) {
    if (name.empty()) {
        outError = "Socket name cannot be empty.";
        return false;
    }
    if (FindSocketByName(name)) {
        outError = "A socket named '" + name + "' already exists.";
        return false;
    }
    if (bone.empty()) {
        outError = "Socket parent bone cannot be empty.";
        return false;
    }
    BeginAuthoredEdit();
    CharacterSocket socket;
    socket.name = name;
    socket.bone = bone;
    asset_.sockets.push_back(socket);
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::DeleteSocket(const std::string& name, std::string& outError) {
    auto it = std::find_if(asset_.sockets.begin(), asset_.sockets.end(),
                           [&](const CharacterSocket& socket) { return socket.name == name; });
    if (it == asset_.sockets.end()) {
        outError = "Socket not found.";
        return false;
    }
    BeginAuthoredEdit();
    asset_.sockets.erase(it);
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::RenameSocket(const std::string& oldName, const std::string& newName,
                                     std::string& outError) {
    if (newName.empty()) {
        outError = "Socket name cannot be empty.";
        return false;
    }
    CharacterSocket* socket = FindSocketByName(oldName);
    if (!socket) {
        outError = "Socket not found.";
        return false;
    }
    if (oldName != newName && FindSocketByName(newName)) {
        outError = "A socket named '" + newName + "' already exists.";
        return false;
    }
    BeginAuthoredEdit();
    socket->name = newName;
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::SetSocketBone(const std::string& name, const std::string& bone,
                                      std::string& outError) {
    CharacterSocket* socket = FindSocketByName(name);
    if (!socket) {
        outError = "Socket not found.";
        return false;
    }
    if (bone.empty()) {
        outError = "Socket parent bone cannot be empty.";
        return false;
    }
    BeginAuthoredEdit();
    socket->bone = bone;
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::SetSocketTransform(const std::string& name, const float position[3],
                                           const float rotation[3], std::string& outError) {
    CharacterSocket* socket = FindSocketByName(name);
    if (!socket) {
        outError = "Socket not found.";
        return false;
    }
    BeginAuthoredEdit();
    socket->position[0] = position[0];
    socket->position[1] = position[1];
    socket->position[2] = position[2];
    socket->rotation[0] = rotation[0];
    socket->rotation[1] = rotation[1];
    socket->rotation[2] = rotation[2];
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::SetMaterialSlot(int slotIndex, const std::string& materialPath,
                                        std::string& outError) {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(asset_.mesh.submeshes.size())) {
        outError = "Material slot index is invalid.";
        return false;
    }
    BeginAuthoredEdit();
    asset_.mesh.submeshes[static_cast<size_t>(slotIndex)].material = materialPath;
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::SetControllerEnabled(bool enabled) {
    BeginAuthoredEdit();
    asset_.controller.enabled = enabled;
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::SetControllerScript(const std::string& scriptPath) {
    BeginAuthoredEdit();
    asset_.controller.scriptPath = scriptPath;
    MarkAuthoredChanged();
    return true;
}

bool CharacterDocument::SetDefaultAnimation(const std::string& clipId, std::string& outError) {
    if (!clipId.empty() && !FindClipById(clipId)) {
        outError = "Clip not found.";
        return false;
    }
    BeginAuthoredEdit();
    asset_.defaults.animation = clipId;
    MarkAuthoredChanged();
    return true;
}

std::string CharacterDocument::SerializeForUndo() {
    return CharacterSerializer::SerializeToString(asset_);
}

bool CharacterDocument::DeserializeFromUndo(const std::string& data, std::string& outError) {
    if (!CharacterSerializer::DeserializeFromString(asset_, data, outError)) {
        return false;
    }
    SetDirty(true);
    rebuildPreviewWorld();
    return true;
}

} // namespace Hephaestus
