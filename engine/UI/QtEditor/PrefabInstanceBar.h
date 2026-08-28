#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <entt/entt.hpp>
#include <string>

namespace Hephaestus {

class EditorContext;

namespace QtEditor {

class PrefabInstanceBar : public QGroupBox
{
    Q_OBJECT

public:
    explicit PrefabInstanceBar(QWidget* parent = nullptr);

    void Bind(EditorContext* ctx, entt::entity entity);
    void Clear();
    bool IsBound() const { return m_entity != entt::null; }

signals:
    void revertPropertyRequested(const QString& component, const QString& property);
    void revertComponentRequested(const QString& component);
    void revertNodeRequested();
    void revertAllRequested();
    void applyPropertyRequested(const QString& component, const QString& property);
    void applyComponentRequested(const QString& component);
    void applyNodeRequested();
    void applyAllRequested();
    void refreshRequested();
    void openPrefabRequested();

private:
    void rebuildOverrideList();

    EditorContext* m_ctx = nullptr;
    entt::entity m_entity = entt::null;

    QLabel* m_pathLabel = nullptr;
    QLabel* m_warningLabel = nullptr;
    QWidget* m_overrideContainer = nullptr;
    QVBoxLayout* m_overrideLayout = nullptr;
    QPushButton* m_revertAllBtn = nullptr;
    QPushButton* m_applyAllBtn = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QPushButton* m_openBtn = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
