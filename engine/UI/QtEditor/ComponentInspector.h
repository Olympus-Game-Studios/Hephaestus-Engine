#pragma once

/**
 * @file ComponentInspector.h
 * @brief Base class and registry for component inspector widgets.
 *
 * Architecture:
 *   Each ECS component type that needs a Details panel section registers
 *   a factory via ComponentInspectorRegistry::Register<ComponentType>().
 *   The Details panel calls CreateInspectors() for the selected entity;
 *   each matching factory produces a QGroupBox-based widget that reads
 *   from and writes to the ECS component directly.
 *
 * To add a new component inspector:
 *   1.  Create a class that inherits ComponentInspector.
 *   2.  Implement buildUI(), connectSignals(), Refresh(), applyToComponent().
 *   3.  In EditorMainWindow::initializeEngine() (or a setup function),
 *       call ComponentInspectorRegistry::Register<YourComponent>(factory).
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QGroupBox>
#include <QVBoxLayout>
#include <entt/entt.hpp>
#include <functional>
#include <string>
#include <vector>
#include <memory>

namespace Hephaestus {

class EditorContext;

namespace QtEditor {

// ---------------------------------------------------------------------------
// ComponentInspector — base widget for any component section in Details panel
// ---------------------------------------------------------------------------

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
    {}

    ~ComponentInspector() override = default;

    /** Re-read component values into widgets. */
    virtual void Refresh() = 0;

    /** Return the entity this inspector is bound to. */
    entt::entity GetEntity() const { return m_entity; }

signals:
    /** Emitted when this inspector wants to remove the component. */
    void componentRemoved(entt::entity entity, const QString& componentName);

protected:
    EditorContext* m_ctx    = nullptr;
    entt::entity   m_entity = entt::null;
    bool           m_updating = false;   // guard against re-entrant writes
};

// ---------------------------------------------------------------------------
// Factory type: (EditorContext*, entity, parentWidget) -> inspector widget
// ---------------------------------------------------------------------------

using InspectorFactory = std::function<
    ComponentInspector*(EditorContext*, entt::entity, QWidget*)>;

/**
 * @brief Predicate that tests whether an entity should show this inspector.
 *        Typically checks registry.all_of<ComponentType>(entity).
 */
using InspectorPredicate = std::function<
    bool(const entt::registry&, entt::entity)>;

// ---------------------------------------------------------------------------
// Registration entry
// ---------------------------------------------------------------------------

struct InspectorEntry {
    std::string       name;       // e.g. "MeshRenderer"
    int               order;      // sort key (lower = higher in panel)
    InspectorPredicate predicate;
    InspectorFactory   factory;
};

// ---------------------------------------------------------------------------
// ComponentInspectorRegistry — singleton that stores available factories
// ---------------------------------------------------------------------------

class ComponentInspectorRegistry
{
public:
    static ComponentInspectorRegistry& Instance()
    {
        static ComponentInspectorRegistry s_instance;
        return s_instance;
    }

    /**
     * Register an inspector factory.
     *
     * @param name       Human-readable component name (must be unique).
     * @param order      Sort position in Details panel (0 = top).
     * @param predicate  Returns true if entity has the component.
     * @param factory    Creates the inspector widget.
     */
    void Register(const std::string& name, int order,
                  InspectorPredicate predicate, InspectorFactory factory)
    {
        m_entries.push_back({ name, order, std::move(predicate), std::move(factory) });
    }

    /**
     * Create inspector widgets for every component the entity has.
     * Returned in sorted order.
     */
    std::vector<ComponentInspector*> CreateInspectors(
        EditorContext* ctx, entt::entity entity, const entt::registry& reg,
        QWidget* parent) const
    {
        // Collect matching entries
        std::vector<const InspectorEntry*> matching;
        for (auto& e : m_entries) {
            if (e.predicate(reg, entity)) {
                matching.push_back(&e);
            }
        }
        // Sort by order
        std::sort(matching.begin(), matching.end(),
                  [](const InspectorEntry* a, const InspectorEntry* b) {
                      return a->order < b->order;
                  });
        // Build widgets
        std::vector<ComponentInspector*> out;
        out.reserve(matching.size());
        for (auto* e : matching) {
            if (auto* w = e->factory(ctx, entity, parent)) {
                out.push_back(w);
            }
        }
        return out;
    }

    /** Names of all registered inspectors (for "Add Component" filtering). */
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
