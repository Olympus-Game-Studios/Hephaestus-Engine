#if HEPHAESTUS_EDITOR_QT_UI

#include "PrefabInstanceBar.h"

#include "../../Core/EditorContext.h"
#include "../../Scene/Scene.h"
#include "../../Scene/Components.h"
#include "../../Scene/PrefabOverrideSystem.h"

#include <QMenu>

namespace Hephaestus {
namespace QtEditor {

PrefabInstanceBar::PrefabInstanceBar(QWidget* parent)
    : QGroupBox(tr("Prefab Instance"), parent)
{
    setObjectName("PrefabInstanceBar");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 12, 8, 8);
    layout->setSpacing(4);

    m_pathLabel = new QLabel(this);
    m_pathLabel->setWordWrap(true);
    m_pathLabel->setStyleSheet("QLabel { color: #9DA3AE; font-size: 8pt; }");
    layout->addWidget(m_pathLabel);

    m_warningLabel = new QLabel(this);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setStyleSheet("QLabel { color: #E6A5A5; font-size: 9pt; }");
    m_warningLabel->hide();
    layout->addWidget(m_warningLabel);

    m_overrideContainer = new QWidget(this);
    m_overrideLayout = new QVBoxLayout(m_overrideContainer);
    m_overrideLayout->setContentsMargins(0, 0, 0, 0);
    m_overrideLayout->setSpacing(2);
    layout->addWidget(m_overrideContainer);

    auto* buttonRow = new QHBoxLayout();
    m_revertAllBtn = new QPushButton(tr("Revert All"), this);
    m_applyAllBtn = new QPushButton(tr("Apply All"), this);
    m_refreshBtn = new QPushButton(tr("Refresh"), this);
    m_openBtn = new QPushButton(tr("Open"), this);
    buttonRow->addWidget(m_revertAllBtn);
    buttonRow->addWidget(m_applyAllBtn);
    buttonRow->addWidget(m_refreshBtn);
    buttonRow->addWidget(m_openBtn);
    layout->addLayout(buttonRow);

    connect(m_revertAllBtn, &QPushButton::clicked, this, &PrefabInstanceBar::revertAllRequested);
    connect(m_applyAllBtn, &QPushButton::clicked, this, &PrefabInstanceBar::applyAllRequested);
    connect(m_refreshBtn, &QPushButton::clicked, this, &PrefabInstanceBar::refreshRequested);
    connect(m_openBtn, &QPushButton::clicked, this, &PrefabInstanceBar::openPrefabRequested);

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction(tr("Revert Node"), this, [this]() { emit revertNodeRequested(); });
        menu.addAction(tr("Apply Node to Prefab"), this, [this]() { emit applyNodeRequested(); });
        menu.exec(mapToGlobal(pos));
    });

    hide();
}

void PrefabInstanceBar::Clear()
{
    m_ctx = nullptr;
    m_entity = entt::null;
    m_pathLabel->clear();
    m_warningLabel->hide();
    while (QLayoutItem* item = m_overrideLayout->takeAt(0)) {
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }
    hide();
}

void PrefabInstanceBar::Bind(EditorContext* ctx, entt::entity entity)
{
    m_ctx = ctx;
    m_entity = entity;
    rebuildOverrideList();
}

void PrefabInstanceBar::rebuildOverrideList()
{
    while (QLayoutItem* item = m_overrideLayout->takeAt(0)) {
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }

    if (!m_ctx || !m_ctx->HasActiveScene() || m_entity == entt::null) {
        hide();
        return;
    }

    Scene* scene = m_ctx->GetActiveScene();
    Entity entity(m_entity, scene);
    if (!entity || !PrefabOverrideSystem::IsPrefabInstanceEntity(entity)) {
        hide();
        return;
    }

    Entity root = PrefabOverrideSystem::FindInstanceRoot(*scene, entity);
    if (!root || !root.HasComponent<PrefabInstanceComponent>()) {
        hide();
        return;
    }

    const auto& inst = root.GetComponent<PrefabInstanceComponent>();
    show();
    setTitle(inst.missingPrefab ? tr("Missing Prefab") : tr("Prefab Instance"));
    m_pathLabel->setText(QString::fromStdString(inst.prefabPath));
    if (inst.missingPrefab) {
        m_warningLabel->setText(tr("The source prefab could not be loaded.\n%1")
            .arg(QString::fromStdString(inst.missingReason)));
        m_warningLabel->show();
    } else {
        m_warningLabel->hide();
    }

    const auto* nodeComp = PrefabOverrideSystem::TryGetNode(entity);
    const std::string nodeId = nodeComp ? nodeComp->nodeId : inst.rootNodeId;
    const auto refs = PrefabOverrideSystem::ListOverrides(inst);
    int shown = 0;
    for (const auto& ref : refs) {
        if (ref.nodeId != nodeId && shown >= 8) {
            continue;
        }
        auto* row = new QWidget(m_overrideContainer);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);
        const bool onThisNode = (ref.nodeId == nodeId);
        auto* label = new QLabel(
            QString::fromStdString(ref.component + "." + ref.property), row);
        label->setStyleSheet(onThisNode
            ? "QLabel { color: #c9a227; font-size: 8pt; }"
            : "QLabel { color: #8a8070; font-size: 8pt; }");
        rowLayout->addWidget(label, 1);
        auto* revertBtn = new QPushButton(tr("Revert"), row);
        revertBtn->setFixedHeight(18);
        auto* applyBtn = new QPushButton(tr("Apply"), row);
        applyBtn->setFixedHeight(18);
        const QString component = QString::fromStdString(ref.component);
        const QString property = QString::fromStdString(ref.property);
        connect(revertBtn, &QPushButton::clicked, this, [this, component, property]() {
            emit revertPropertyRequested(component, property);
        });
        connect(applyBtn, &QPushButton::clicked, this, [this, component, property]() {
            emit applyPropertyRequested(component, property);
        });
        rowLayout->addWidget(revertBtn);
        rowLayout->addWidget(applyBtn);
        m_overrideLayout->addWidget(row);
        ++shown;
        if (shown >= 12) {
            break;
        }
    }

    m_revertAllBtn->setEnabled(!inst.missingPrefab && PrefabOverrideSystem::HasAnyOverride(inst));
    m_applyAllBtn->setEnabled(!inst.missingPrefab && PrefabOverrideSystem::HasAnyOverride(inst));
    m_openBtn->setEnabled(!inst.prefabPath.empty());
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
