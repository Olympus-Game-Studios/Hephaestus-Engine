#include "Scene.h"
#include "../Construction/ConstructionShape.h"
#include "PrefabComponentIO.h"
#include "PrefabInstantiator.h"
#include "PrefabOverrideSystem.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <functional>
#include <filesystem>
#include <unordered_set>
#include <vector>

namespace {
std::atomic<std::uint64_t> s_NextSceneIdentity{1};
}

Scene::Scene(const std::string& name)
    : m_Name(name)
    , m_SceneIdentity(s_NextSceneIdentity.fetch_add(1, std::memory_order_relaxed))
{
}

Scene::~Scene()
{
    Clear();
}

void Scene::SetSourceAssetPath(const std::string& path)
{
    if (path.empty()) {
        m_SourceAssetPath.clear();
        return;
    }

    // URI paths must be tested before filesystem normalization. On Windows,
    // std::filesystem treats the colon in plugin:// as a path/root delimiter.
    if (path.find("://") != std::string::npos) {
        m_SourceAssetPath = path;
        return;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path absolute = fs::absolute(fs::u8path(path), ec);
    if (ec) {
        m_SourceAssetPath = fs::u8path(path).lexically_normal().generic_string();
        return;
    }
    fs::path canonical = fs::weakly_canonical(absolute, ec);
    m_SourceAssetPath = (ec ? absolute.lexically_normal() : canonical).generic_string();
}

Entity Scene::CreateEntity(const std::string& name)
{
    Entity entity = { m_Registry.create(), this };
    entity.AddComponent<TagComponent>(name);
    entity.AddComponent<TransformComponent>();
    return entity;
}

Entity Scene::CreateEntityWithID(entt::entity id, const std::string& name)
{
    Entity entity = { m_Registry.create(id), this };
    entity.AddComponent<TagComponent>(name);
    entity.AddComponent<TransformComponent>();
    return entity;
}

void Scene::DestroyEntity(Entity entity)
{
    if (!entity)
        return;

    // Remove from parent's children list
    if (entity.HasComponent<TransformComponent>()) {
        auto& transform = entity.GetComponent<TransformComponent>();
        if (transform.parent != entt::null) {
            Entity parentEntity = { transform.parent, this };
            if (parentEntity && parentEntity.HasComponent<TransformComponent>()) {
                auto& parentTransform = parentEntity.GetComponent<TransformComponent>();
                auto it = std::find(parentTransform.children.begin(), 
                                  parentTransform.children.end(), 
                                  entity.m_EntityHandle);
                if (it != parentTransform.children.end()) {
                    parentTransform.children.erase(it);
                }
            }
        }

        // Reparent or destroy children
        for (auto childHandle : transform.children) {
            Entity child = { childHandle, this };
            if (child) {
                // Option 1: Reparent to world root (remove parent)
                RemoveParent(child);
                // Option 2: Recursively destroy children
                // DestroyEntity(child);
            }
        }
    }

    m_Registry.destroy(entity.m_EntityHandle);
}

void Scene::SetParent(Entity child, Entity parent, bool preserveWorldTransform)
{
    if (!child || !parent || child == parent)
        return;

    // Prevent circular dependencies
    Entity current = parent;
    while (current) {
        if (current == child)
            return; // Would create a cycle
        current = GetParent(current);
    }

    const Mat4 childWorld = preserveWorldTransform ? GetWorldTransform(child) : Mat4::Identity();
    auto& childTransform = child.GetComponent<TransformComponent>();

    // Remove from old parent
    if (childTransform.parent != entt::null) {
        Entity oldParent = { childTransform.parent, this };
        if (oldParent && oldParent.HasComponent<TransformComponent>()) {
            auto& oldParentTransform = oldParent.GetComponent<TransformComponent>();
            auto it = std::find(oldParentTransform.children.begin(),
                              oldParentTransform.children.end(),
                              child.m_EntityHandle);
            if (it != oldParentTransform.children.end()) {
                oldParentTransform.children.erase(it);
            }
        }
    }

    // Add to new parent
    childTransform.parent = parent.m_EntityHandle;
    auto& parentTransform = parent.GetComponent<TransformComponent>();
    parentTransform.children.push_back(child.m_EntityHandle);

    if (preserveWorldTransform) {
        const Mat4 parentWorld = GetWorldTransform(parent);
        childTransform.SetFromMatrix(Mat4::Multiply(Mat4::Invert(parentWorld), childWorld));
    }
}

void Scene::RemoveParent(Entity child, bool preserveWorldTransform)
{
    if (!child || !child.HasComponent<TransformComponent>())
        return;

    const Mat4 childWorld = preserveWorldTransform ? GetWorldTransform(child) : Mat4::Identity();
    auto& childTransform = child.GetComponent<TransformComponent>();
    
    if (childTransform.parent != entt::null) {
        Entity parent = { childTransform.parent, this };
        if (parent && parent.HasComponent<TransformComponent>()) {
            auto& parentTransform = parent.GetComponent<TransformComponent>();
            auto it = std::find(parentTransform.children.begin(),
                              parentTransform.children.end(),
                              child.m_EntityHandle);
            if (it != parentTransform.children.end()) {
                parentTransform.children.erase(it);
            }
        }
        childTransform.parent = entt::null;
        if (preserveWorldTransform) {
            childTransform.SetFromMatrix(childWorld);
        }
    }
}

Entity Scene::GetParent(Entity entity)
{
    if (!entity || !entity.HasComponent<TransformComponent>())
        return {};

    auto& transform = entity.GetComponent<TransformComponent>();
    if (transform.parent != entt::null) {
        return { transform.parent, this };
    }
    return {};
}

std::vector<Entity> Scene::GetChildren(Entity entity)
{
    std::vector<Entity> children;
    if (!entity || !entity.HasComponent<TransformComponent>())
        return children;

    auto& transform = entity.GetComponent<TransformComponent>();
    for (auto childHandle : transform.children) {
        children.push_back({ childHandle, this });
    }
    return children;
}

Mat4 Scene::GetWorldTransform(Entity entity)
{
    if (!entity || !entity.HasComponent<TransformComponent>())
        return Mat4::Identity();

    auto& transform = entity.GetComponent<TransformComponent>();
    Mat4 localTransform = transform.GetLocalMatrix();

    if (transform.parent != entt::null) {
        Entity parent = { transform.parent, this };
        Mat4 parentTransform = GetWorldTransform(parent);
        return Mat4::Multiply(parentTransform, localTransform);
    }

    return localTransform;
}

Entity Scene::FindEntityByName(const std::string& name)
{
    auto view = m_Registry.view<TagComponent>();
    for (auto entity : view) {
        auto& tag = view.get<TagComponent>(entity);
        if (tag.tag == name) {
            return { entity, this };
        }
    }
    return {};
}

std::vector<Entity> Scene::FindEntitiesWithNamePrefix(const std::string& prefix)
{
    std::vector<Entity> result;
    auto view = m_Registry.view<TagComponent>();
    for (auto entity : view) {
        auto& tag = view.get<TagComponent>(entity);
        if (prefix.empty() || tag.tag.rfind(prefix, 0) == 0) {
            result.emplace_back(entity, this);
        }
    }
    return result;
}

Entity Scene::GetSelectedEntity()
{
    if (m_PrimarySelected != entt::null && m_Registry.valid(m_PrimarySelected)) {
        Entity primary = { m_PrimarySelected, this };
        if (primary.HasComponent<SelectionComponent>() && primary.GetComponent<SelectionComponent>().selected) {
            return primary;
        }
    }

    auto view = m_Registry.view<SelectionComponent>();
    for (auto entity : view) {
        auto& selection = view.get<SelectionComponent>(entity);
        if (selection.selected) {
            m_PrimarySelected = entity;
            return { entity, this };
        }
    }
    return {};
}

std::vector<Entity> Scene::GetSelectedEntities()
{
    std::vector<Entity> out;
    auto view = m_Registry.view<SelectionComponent>();
    for (auto entity : view) {
        auto& selection = view.get<SelectionComponent>(entity);
        if (selection.selected) {
            out.emplace_back(entity, this);
        }
    }
    return out;
}

void Scene::SetSelectedEntity(Entity entity)
{
    // Clear all selections
    ClearSelection();

    // Select the new entity
    if (entity) {
        if (!entity.HasComponent<SelectionComponent>()) {
            entity.AddComponent<SelectionComponent>(true);
        } else {
            entity.GetComponent<SelectionComponent>().selected = true;
        }
        m_PrimarySelected = static_cast<entt::entity>(entity);
    } else {
        m_PrimarySelected = entt::null;
    }
}

void Scene::SetSelectedEntities(const std::vector<entt::entity>& entities, entt::entity primary)
{
    ClearSelection();
    if (entities.empty()) {
        return;
    }

    for (entt::entity e : entities) {
        if (e == entt::null || !m_Registry.valid(e)) {
            continue;
        }
        Entity ent(e, this);
        if (!ent.HasComponent<SelectionComponent>()) {
            ent.AddComponent<SelectionComponent>(true);
        } else {
            ent.GetComponent<SelectionComponent>().selected = true;
        }
    }

    if (primary != entt::null && m_Registry.valid(primary)) {
        m_PrimarySelected = primary;
    } else {
        for (entt::entity e : entities) {
            if (e != entt::null && m_Registry.valid(e)) {
                m_PrimarySelected = e;
                break;
            }
        }
    }
}

void Scene::AddSelectedEntity(Entity entity)
{
    if (!entity)
        return;

    if (!entity.HasComponent<SelectionComponent>()) {
        entity.AddComponent<SelectionComponent>(true);
    } else {
        entity.GetComponent<SelectionComponent>().selected = true;
    }

    m_PrimarySelected = static_cast<entt::entity>(entity);
}

void Scene::ToggleSelectedEntity(Entity entity)
{
    if (!entity)
        return;

    bool nowSelected = true;
    if (!entity.HasComponent<SelectionComponent>()) {
        entity.AddComponent<SelectionComponent>(true);
        nowSelected = true;
    } else {
        auto& sel = entity.GetComponent<SelectionComponent>();
        sel.selected = !sel.selected;
        nowSelected = sel.selected;
    }

    if (nowSelected) {
        m_PrimarySelected = static_cast<entt::entity>(entity);
        return;
    }

    if (m_PrimarySelected == static_cast<entt::entity>(entity)) {
        m_PrimarySelected = entt::null;
        auto view = m_Registry.view<SelectionComponent>();
        for (auto ent : view) {
            auto& selection = view.get<SelectionComponent>(ent);
            if (selection.selected) {
                m_PrimarySelected = ent;
                break;
            }
        }
    }
}

void Scene::ClearSelection()
{
    auto view = m_Registry.view<SelectionComponent>();
    std::vector<entt::entity> toClear;
    for (auto entity : view) {
        toClear.push_back(entity);
    }
    for (auto entity : toClear) {
        m_Registry.remove<SelectionComponent>(entity);
    }
    m_PrimarySelected = entt::null;
}

void Scene::Clear()
{
    m_Registry.clear();
    m_PrimarySelected = entt::null;
}

namespace {
// An entity and its children as Duplicate and Paste recreate them.
struct EntitySnapshot {
    std::string name;
    std::optional<TransformComponent> transform;
    std::vector<PrefabComponentEntry> components;
    std::optional<AudioSourceComponent> audio;
    std::optional<FolderComponent> folder;
    std::optional<PrefabInstanceComponent> prefabInstance;
    std::vector<EntitySnapshot> children;
};

std::optional<EntitySnapshot> s_EntityClipboard;

Entity PrefabRootOf(Scene& scene, Entity entity)
{
    if (entity.HasComponent<PrefabNodeComponent>()) {
        Entity prefabRoot = PrefabOverrideSystem::FindInstanceRoot(scene, entity);
        if (prefabRoot) return prefabRoot;
    }
    return entity;
}

std::string UniqueEntityName(Scene& scene, const std::string& original)
{
    std::string base = original.empty() ? "Entity" : original;
    const size_t suffixStart = base.rfind(" (");
    if (suffixStart != std::string::npos && base.back() == ')') {
        const std::string number = base.substr(suffixStart + 2, base.size() - suffixStart - 3);
        if (!number.empty() &&
            std::all_of(number.begin(), number.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            base.resize(suffixStart);
        }
    }
    std::string candidate = base;
    int suffix = 1;
    while (scene.FindEntityByName(candidate)) {
        candidate = base + " (" + std::to_string(suffix++) + ")";
    }
    return candidate;
}

EntitySnapshot CaptureSnapshot(Scene& scene, Entity source)
{
    EntitySnapshot snapshot;
    snapshot.name = source.HasComponent<TagComponent>() ? source.GetComponent<TagComponent>().tag : "Entity";
    if (source.HasComponent<PrefabInstanceComponent>()) {
        snapshot.prefabInstance = source.GetComponent<PrefabInstanceComponent>();
        return snapshot;
    }
    if (source.HasComponent<TransformComponent>()) {
        snapshot.transform = source.GetComponent<TransformComponent>();
    }
    PrefabComponentIO::CaptureFromEntity(source, snapshot.components);
    if (source.HasComponent<AudioSourceComponent>()) {
        AudioSourceComponent audio = source.GetComponent<AudioSourceComponent>();
        audio.runtimeHandle = 0;
        audio.runtimeStarted = false;
        audio.runtimeLastClip.clear();
        snapshot.audio = audio;
    }
    if (source.HasComponent<FolderComponent>()) {
        snapshot.folder = source.GetComponent<FolderComponent>();
    }
    for (Entity child : scene.GetChildren(source)) {
        if (!child.HasComponent<ConstructionGeneratedComponent>()) {
            snapshot.children.push_back(CaptureSnapshot(scene, child));
        }
    }
    return snapshot;
}

Entity InstantiateSnapshot(Scene& scene, const EntitySnapshot& snapshot, Entity newParent)
{
    if (snapshot.prefabInstance) {
        PrefabAsset asset;
        std::string error;
        if (!PrefabOverrideSystem::LoadAsset(snapshot.prefabInstance->prefabPath, asset, error)) {
            return {};
        }
        PrefabInstanceComponent instanceCopy = *snapshot.prefabInstance;
        instanceCopy.instanceId.clear();
        instanceCopy.parent = newParent ? static_cast<entt::entity>(newParent) : entt::null;

        PrefabInstantiateOptions options;
        options.prefabPath = snapshot.prefabInstance->prefabPath;
        options.parent = instanceCopy.parent;
        options.instanceData = &instanceCopy;
        Entity duplicate = PrefabInstantiator::Instantiate(scene, asset, options);
        if (duplicate && duplicate.HasComponent<TagComponent>()) {
            duplicate.GetComponent<TagComponent>().tag = UniqueEntityName(scene, snapshot.name);
        }
        return duplicate;
    }

    Entity duplicate = scene.CreateEntity(UniqueEntityName(scene, snapshot.name));
    if (snapshot.transform) {
        PrefabComponentIO::CopyLocalTransform(duplicate.GetComponent<TransformComponent>(), *snapshot.transform);
    }
    for (const PrefabComponentEntry& component : snapshot.components) {
        PrefabComponentIO::ApplyEntry(duplicate, component);
    }
    if (snapshot.audio) {
        duplicate.AddComponent<AudioSourceComponent>(*snapshot.audio);
    }
    if (snapshot.folder) {
        duplicate.AddComponent<FolderComponent>(*snapshot.folder);
    }
    if (newParent) {
        scene.SetParent(duplicate, newParent);
    }
    for (const EntitySnapshot& child : snapshot.children) {
        InstantiateSnapshot(scene, child, duplicate);
    }
    return duplicate;
}
} // namespace

void Scene::CopyEntityToClipboard(Entity entity)
{
    if (!entity) {
        s_EntityClipboard.reset();
        return;
    }
    s_EntityClipboard = CaptureSnapshot(*this, PrefabRootOf(*this, entity));
}

bool Scene::HasClipboardEntity() const
{
    return s_EntityClipboard.has_value();
}

Entity Scene::PasteClipboard()
{
    if (!s_EntityClipboard.has_value())
        return {};
    Entity e = InstantiateSnapshot(*this, *s_EntityClipboard, {});
    if (e) SetSelectedEntity(e);
    return e;
}

std::vector<Entity> Scene::DuplicateEntities(const std::vector<entt::entity>& entities,
                                             bool preserveParent)
{
    std::vector<entt::entity> roots;
    std::unordered_set<entt::entity> seen;
    roots.reserve(entities.size());

    for (entt::entity handle : entities) {
        Entity source(handle, this);
        if (!source) {
            continue;
        }
        source = PrefabRootOf(*this, source);
        handle = static_cast<entt::entity>(source);

        bool hasSelectedAncestor = false;
        for (Entity parent = GetParent(source); parent; parent = GetParent(parent)) {
            if (std::find(entities.begin(), entities.end(),
                          static_cast<entt::entity>(parent)) != entities.end()) {
                hasSelectedAncestor = true;
                break;
            }
        }
        if (!hasSelectedAncestor && seen.insert(handle).second) {
            roots.push_back(handle);
        }
    }

    std::vector<Entity> duplicates;
    duplicates.reserve(roots.size());
    for (entt::entity handle : roots) {
        Entity source(handle, this);
        Entity parent = preserveParent ? GetParent(source) : Entity{};
        Entity duplicate = InstantiateSnapshot(*this, CaptureSnapshot(*this, source), parent);
        if (duplicate) {
            duplicates.push_back(duplicate);
        }
    }
    return duplicates;
}
