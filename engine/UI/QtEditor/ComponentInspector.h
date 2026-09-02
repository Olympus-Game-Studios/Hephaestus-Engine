#pragma once

/**
 * @file ComponentInspector.h
 * @brief Base class and registry for component inspector widgets.
 *
 * Architecture:
 *   Each ECS component type that needs a Details panel section registers
 *   a factory via ComponentInspectorRegistry.
 *   Inspectors bind to the shared-component intersection of the current
 *   selection (one widget tree, never one inspector per entity).
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorUndoCommand.h"

#include "../../Core/EditorContext.h"
#include "../../Scene/Scene.h"

#include <QGroupBox>
#include <QMenu>
#include <algorithm>
#include <entt/entt.hpp>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Hephaestus {

namespace QtEditor {

class ComponentInspector : public QGroupBox
{
    Q_OBJECT

public:
    explicit ComponentInspector(const QString& title,
                                EditorContext* ctx,
                                entt::entity entity,
                                QWidget* parent = nullptr)
        : QGroupBox(title, parent)
        , m_ctx(ctx)
        , m_entity(entity)
    {
        if (entity != entt::null) {
            m_targets.push_back(entity);
        }
    }

    ~ComponentInspector() override = default;

    virtual void Refresh() = 0;

    entt::entity GetEntity() const { return m_entity; }
    const std::vector<entt::entity>& Targets() const { return m_targets; }
    void SetTypeName(const std::string& typeName) { m_typeName = typeName; }
    const std::string& TypeName() const { return m_typeName; }

    void SetTargets(std::vector<entt::entity> targets)
    {
        m_targets = std::move(targets);
        if (!m_targets.empty()) {
            m_entity = m_targets.front();
        }
        EndEdit();
        Refresh();
    }

    void BeginEdit(const char* label)
    {
        if (m_editActive || !m_ctx) return;
        EditorUndoManager::PushSnapshot(m_ctx, label ? label : "Edit component");
        m_editActive = true;
    }

    void EndEdit() { m_editActive = false; }

    template<typename T, typename Fn>
    void VisitTargets(Fn&& fn)
    {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg = m_ctx->GetActiveScene()->GetRegistry();
        for (entt::entity entity : m_targets) {
            if (!reg.valid(entity) || !reg.all_of<T>(entity)) continue;
            fn(reg.get<T>(entity), entity);
        }
    }

    template<typename T, typename Fn>
    void ForEachTarget(Fn&& fn)
    {
        if (m_updating) return;
        VisitTargets<T>(std::forward<Fn>(fn));
        if (m_ctx) m_ctx->MarkDirty();
    }

signals:
    void componentRemoved(entt::entity entity, const QString& componentName);
    void copyComponentRequested(const QString& typeName);
    void pasteValuesRequested(const QString& typeName);
    void resetComponentRequested(const QString& typeName);

protected:
    EditorContext* m_ctx    = nullptr;
    entt::entity   m_entity = entt::null;
    std::vector<entt::entity> m_targets;
    std::string m_typeName;
    bool m_updating = false;
    bool m_editActive = false;
};

using InspectorFactory = std::function<
    ComponentInspector*(EditorContext*, entt::entity, QWidget*)>;

using InspectorPredicate = std::function<
    bool(const entt::registry&, entt::entity)>;

struct InspectorEntry {
    std::string       name;
    int               order;
    InspectorPredicate predicate;
    InspectorFactory   factory;
};

class ComponentInspectorRegistry
{
public:
    static ComponentInspectorRegistry& Instance()
    {
        static ComponentInspectorRegistry s_instance;
        return s_instance;
    }

    void Register(const std::string& name, int order,
                  InspectorPredicate predicate, InspectorFactory factory)
    {
        m_entries.push_back({ name, order, std::move(predicate), std::move(factory) });
    }

    std::vector<ComponentInspector*> CreateInspectors(
        EditorContext* ctx, entt::entity entity, const entt::registry& reg,
        QWidget* parent) const
    {
        return CreateInspectors(ctx, std::vector<entt::entity>{ entity }, reg, parent);
    }

    std::vector<ComponentInspector*> CreateInspectors(
        EditorContext* ctx, const std::vector<entt::entity>& entities,
        const entt::registry& reg, QWidget* parent) const
    {
        std::vector<const InspectorEntry*> matching;
        for (auto& e : m_entries) {
            bool all = !entities.empty();
            for (entt::entity entity : entities) {
                if (!e.predicate(reg, entity)) {
                    all = false;
                    break;
                }
            }
            if (all) {
                matching.push_back(&e);
            }
        }
        std::sort(matching.begin(), matching.end(),
                  [](const InspectorEntry* a, const InspectorEntry* b) {
                      return a->order < b->order;
                  });
        std::vector<ComponentInspector*> out;
        out.reserve(matching.size());
        const entt::entity primary = entities.empty() ? entt::null : entities.front();
        for (auto* e : matching) {
            if (auto* w = e->factory(ctx, primary, parent)) {
                w->SetTypeName(e->name);
                w->SetTargets(entities);
                out.push_back(w);
            }
        }
        return out;
    }

    std::vector<std::string> GetRegisteredNames() const
    {
        std::vector<std::string> names;
        names.reserve(m_entries.size());
        for (auto& e : m_entries) names.push_back(e.name);
        return names;
    }

    void Clear() { m_entries.clear(); }

private:
    ComponentInspectorRegistry() = default;
    std::vector<InspectorEntry> m_entries;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
