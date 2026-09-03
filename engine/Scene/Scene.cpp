#include "Scene.h"
#include "PrefabComponentIO.h"
#include "PrefabInstantiator.h"
#include "PrefabOverrideSystem.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_set>
#include <vector>

// ── Editor-only: auto-attach billboard icon when a LightComponent is created ──
#ifndef HEPHAESTUS_GAME_RUNTIME
static void onLightComponentAdded(entt::registry& reg, entt::entity ent)
{
    if (!reg.all_of<EditorIconComponent>(ent)) {
        auto& light = reg.get<LightComponent>(ent);
        reg.emplace<EditorIconComponent>(ent, EditorIconComponent::ForLight(light.type));
    }
}
#endif

Scene::Scene(const std::string& name)
    : m_Name(name)
{
#ifndef HEPHAESTUS_GAME_RUNTIME
    m_Registry.on_construct<LightComponent>().connect<&onLightComponentAdded>();
#endif
}

Scene::~Scene()
{
    Clear();
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
}

// ---------------- Clipboard (single-entity copy/paste with component preservation) ----------------
namespace {
struct ClipboardEntityData {
    std::string name;
    std::optional<TransformComponent> transform;
    std::optional<MeshRendererComponent> meshRenderer;
    std::optional<MaterialComponent> material;
    std::optional<LightComponent> light;
    std::optional<MeshColliderComponent> meshCollider;
    std::optional<PhysicsShapeComponent> physicsShape;
    std::optional<RigidBodyComponent> rigidBody;
    std::optional<CameraComponent> camera;
    std::optional<CharacterComponent> character;
};
}

static std::optional<ClipboardEntityData> s_EntityClipboard;

void Scene::CopyEntityToClipboard(Entity entity)
{
    if (!entity) {
        s_EntityClipboard.reset();
        return;
    }

    ClipboardEntityData data;
    data.name = entity.HasComponent<TagComponent>() ? entity.GetComponent<TagComponent>().tag : "Entity";

    if (entity.HasComponent<TransformComponent>()) {
        TransformComponent copy = entity.GetComponent<TransformComponent>();
        copy.parent = entt::null;
        copy.children.clear();
        data.transform = copy;
    }

    if (entity.HasComponent<MeshRendererComponent>()) {
        data.meshRenderer = entity.GetComponent<MeshRendererComponent>();
    }

    if (entity.HasComponent<MaterialComponent>()) {
        data.material = entity.GetComponent<MaterialComponent>();
    }

    if (entity.HasComponent<LightComponent>()) {
        data.light = entity.GetComponent<LightComponent>();
    }

    if (entity.HasComponent<MeshColliderComponent>()) {
        MeshColliderComponent copy = entity.GetComponent<MeshColliderComponent>();
        copy.contacts.clear();
        data.meshCollider = copy;
    }

    if (entity.HasComponent<PhysicsShapeComponent>()) {
        data.physicsShape = entity.GetComponent<PhysicsShapeComponent>();
    }

    if (entity.HasComponent<RigidBodyComponent>()) {
        RigidBodyComponent copy = entity.GetComponent<RigidBodyComponent>();
        copy.InvalidateBodyHandle();
        data.rigidBody = copy;
    }

    if (entity.HasComponent<CameraComponent>()) {
        data.camera = entity.GetComponent<CameraComponent>();
    }

    if (entity.HasComponent<CharacterComponent>()) {
        CharacterComponent copy = entity.GetComponent<CharacterComponent>();
        copy.runtimeReady = false;
        copy.skeleton = {};
        copy.clips.clear();
        copy.player = {};
        copy.localPoses.clear();
        copy.localMatrices.clear();
        copy.worldPose.clear();
        copy.bonePalette.clear();
        copy.lastError.clear();
        data.character = std::move(copy);
    }

    s_EntityClipboard = data;
}

bool Scene::HasClipboardEntity() const
{
    return s_EntityClipboard.has_value();
}

Entity Scene::PasteClipboard()
{
    if (!s_EntityClipboard.has_value())
        return {};

    ClipboardEntityData data = *s_EntityClipboard;

    std::string baseName = data.name.empty() ? "Entity" : data.name;
    
    // Extract base name by removing any existing " (n)" suffix pattern
    size_t lastParen = baseName.rfind(" (");
    if (lastParen != std::string::npos) {
        size_t endParen = baseName.find(")", lastParen);
        if (endParen != std::string::npos) {
            // Check if the content between parens is just a number
            std::string middle = baseName.substr(lastParen + 2, endParen - lastParen - 2);
            if (!middle.empty() && std::all_of(middle.begin(), middle.end(), ::isdigit)) {
                baseName = baseName.substr(0, lastParen);
            }
        }
    }
    
    std::string candidate = baseName;
    int suffix = 1;
    while (FindEntityByName(candidate)) {
        candidate = baseName + " (" + std::to_string(suffix++) + ")";
    }

    Entity e = CreateEntity(candidate);
    if (!e)
        return {};

    if (data.transform) {
        auto& t = e.GetComponent<TransformComponent>();
        t = *data.transform;
        t.parent = entt::null;
        t.children.clear();
    }

    if (data.meshRenderer) {
        e.AddComponent<MeshRendererComponent>(*data.meshRenderer);
    }

    if (data.material) {
        e.AddComponent<MaterialComponent>(*data.material);
    }

    if (data.light) {
        e.AddComponent<LightComponent>(*data.light);
    }

    if (data.meshCollider) {
        e.AddComponent<MeshColliderComponent>(*data.meshCollider);
    }

    if (data.physicsShape) {
        e.AddComponent<PhysicsShapeComponent>(*data.physicsShape);
    }

    if (data.rigidBody) {
        RigidBodyComponent rb = *data.rigidBody;
        rb.InvalidateBodyHandle();
        e.AddComponent<RigidBodyComponent>(rb);
    }

    if (data.camera) {
        e.AddComponent<CameraComponent>(*data.camera);
    }

    if (data.character) {
        e.AddComponent<CharacterComponent>(*data.character);
    }

    SetSelectedEntity(e);
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

        if (source.HasComponent<PrefabNodeComponent>()) {
            Entity prefabRoot = PrefabOverrideSystem::FindInstanceRoot(*this, source);
            if (prefabRoot) {
                source = prefabRoot;
                handle = static_cast<entt::entity>(prefabRoot);
            }
        }

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

    auto uniqueName = [this](const std::string& original) {
        std::string base = original.empty() ? "Entity" : original;
        const size_t suffixStart = base.rfind(" (");
        if (suffixStart != std::string::npos && base.back() == ')') {
            const std::string number =
                base.substr(suffixStart + 2, base.size() - suffixStart - 3);
            if (!number.empty() &&
                std::all_of(number.begin(), number.end(),
                            [](unsigned char c) { return std::isdigit(c) != 0; })) {
                base.resize(suffixStart);
            }
        }
        std::string candidate = base;
        int suffix = 1;
        while (FindEntityByName(candidate)) {
            candidate = base + " (" + std::to_string(suffix++) + ")";
        }
        return candidate;
    };

    std::function<Entity(Entity, Entity)> duplicateOne;
    duplicateOne = [&](Entity source, Entity newParent) -> Entity {
        if (source.HasComponent<PrefabInstanceComponent>()) {
            const auto& sourceInstance = source.GetComponent<PrefabInstanceComponent>();
            PrefabAsset asset;
            std::string error;
            if (!PrefabOverrideSystem::LoadAsset(sourceInstance.prefabPath, asset, error)) {
                return {};
            }

            PrefabInstanceComponent instanceCopy = sourceInstance;
            instanceCopy.instanceId.clear();
            instanceCopy.parent = newParent
                ? static_cast<entt::entity>(newParent)
                : entt::null;

            PrefabInstantiateOptions options;
            options.prefabPath = sourceInstance.prefabPath;
            options.parent = instanceCopy.parent;
            options.instanceData = &instanceCopy;
            Entity duplicate = PrefabInstantiator::Instantiate(*this, asset, options);
            if (duplicate && duplicate.HasComponent<TagComponent>()) {
                duplicate.GetComponent<TagComponent>().tag =
                    uniqueName(source.GetComponent<TagComponent>().tag);
            }
            return duplicate;
        }

        const std::string sourceName = source.HasComponent<TagComponent>()
            ? source.GetComponent<TagComponent>().tag
            : std::string("Entity");
        Entity duplicate = CreateEntity(uniqueName(sourceName));

        if (source.HasComponent<TransformComponent>()) {
            PrefabComponentIO::CopyLocalTransform(
                duplicate.GetComponent<TransformComponent>(),
                source.GetComponent<TransformComponent>());
        }

        std::vector<PrefabComponentEntry> components;
        if (PrefabComponentIO::CaptureFromEntity(source, components)) {
            for (const PrefabComponentEntry& component : components) {
                PrefabComponentIO::ApplyEntry(duplicate, component);
            }
        }

        if (source.HasComponent<AudioSourceComponent>()) {
            AudioSourceComponent audio = source.GetComponent<AudioSourceComponent>();
            audio.runtimeHandle = 0;
            audio.runtimeStarted = false;
            audio.runtimeLastClip.clear();
            duplicate.AddComponent<AudioSourceComponent>(audio);
        }
        if (source.HasComponent<FolderComponent>()) {
            duplicate.AddComponent<FolderComponent>(
                source.GetComponent<FolderComponent>());
        }

        if (newParent) {
            SetParent(duplicate, newParent);
        }

        for (Entity child : GetChildren(source)) {
            duplicateOne(child, duplicate);
        }
        return duplicate;
    };

    std::vector<Entity> duplicates;
    duplicates.reserve(roots.size());
    for (entt::entity handle : roots) {
        Entity source(handle, this);
        Entity parent = preserveParent ? GetParent(source) : Entity{};
        Entity duplicate = duplicateOne(source, parent);
        if (duplicate) {
            duplicates.push_back(duplicate);
        }
    }
    return duplicates;
}
