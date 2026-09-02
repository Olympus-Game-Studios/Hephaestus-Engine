#if HEPHAESTUS_EDITOR_QT_UI

#include "PrefabInspectorPanel.h"
#include "ComponentInspector.h"
#include "AddComponentPopup.h"
#include "../../UI/InspectorSelectionModel.h"
#include "../../Core/EditorContext.h"

#include <QApplication>
#include <QCursor>
#include <QAbstractSpinBox>

namespace {
constexpr float kRadToDeg = 180.0f / 3.14159265358979f;
constexpr float kDegToRad = 3.14159265358979f / 180.0f;
}

namespace Hephaestus {
namespace QtEditor {

static QDoubleSpinBox* makeTransformSpin(QWidget* parent) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setDecimals(4);
    spin->setRange(-999999.0, 999999.0);
    spin->setSingleStep(0.1);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setFixedHeight(22);
    spin->setMinimumWidth(60);
    return spin;
}

static QWidget* makeVec3Row(const QString& label, QDoubleSpinBox*& x, QDoubleSpinBox*& y,
                             QDoubleSpinBox*& z, QWidget* parent)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 1, 0, 1);
    layout->setSpacing(4);

    auto* lbl = new QLabel(label, row);
    lbl->setFixedWidth(28);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(lbl);

    auto makeAxisLabel = [&](const QString& axis, const QString& color) {
        auto* al = new QLabel(axis, row);
        al->setFixedWidth(12);
        al->setAlignment(Qt::AlignCenter);
        al->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; font-size: 10px; }").arg(color));
        return al;
    };

    layout->addWidget(makeAxisLabel("X", "#e06060"));
    x = makeTransformSpin(row);
    layout->addWidget(x, 1);

    layout->addWidget(makeAxisLabel("Y", "#60c060"));
    y = makeTransformSpin(row);
    layout->addWidget(y, 1);

    layout->addWidget(makeAxisLabel("Z", "#6080e0"));
    z = makeTransformSpin(row);
    layout->addWidget(z, 1);

    return row;
}

PrefabInspectorPanel::PrefabInspectorPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_headerLabel = new QLabel(tr("Inspector"), this);
    m_headerLabel->setStyleSheet("QLabel { font-weight: bold; padding: 6px 8px; "
                                  "background: #2a2a2a; border-bottom: 1px solid #3a3a3a; }");
    outerLayout->addWidget(m_headerLabel);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet("QScrollArea { border: none; background: #1e1e1e; }");

    m_contentWidget = new QWidget();
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(8, 8, 8, 8);
    m_contentLayout->setSpacing(6);

    m_nodeGroup = new QGroupBox(tr("Node"), m_contentWidget);
    m_nodeGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid #3a3a3a; "
                               "border-radius: 3px; margin-top: 8px; padding-top: 14px; }");
    auto* nodeLayout = new QVBoxLayout(m_nodeGroup);
    m_nameEdit = new QLineEdit(m_nodeGroup);
    m_nameEdit->setPlaceholderText(tr("Node Name"));
    m_nameEdit->setStyleSheet("QLineEdit { padding: 4px; background: #2d2d2d; "
                              "border: 1px solid #3a3a3a; border-radius: 2px; }");
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (m_updatingUI || m_currentNodeId.empty()) return;
        emit nameChanged(m_currentNodeId, text.toStdString());
    });
    nodeLayout->addWidget(m_nameEdit);
    m_contentLayout->addWidget(m_nodeGroup);

    buildTransformGroup();
    m_contentLayout->addWidget(m_transformGroup);

    m_componentsContainer = new QWidget(m_contentWidget);
    m_componentsLayout = new QVBoxLayout(m_componentsContainer);
    m_componentsLayout->setContentsMargins(0, 0, 0, 0);
    m_componentsLayout->setSpacing(4);
    m_contentLayout->addWidget(m_componentsContainer);

    m_addComponentBtn = new QPushButton(tr("+ Add Component"), m_contentWidget);
    m_addComponentBtn->setStyleSheet(
        "QPushButton { padding: 6px; background: #2a4a2a; border: 1px solid #3a5a3a; "
        "border-radius: 3px; font-weight: bold; }"
        "QPushButton:hover { background: #3a5a3a; }"
        "QPushButton:pressed { background: #2a3a2a; }"
    );
    connect(m_addComponentBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentNodeId.empty()) return;
        std::vector<AddComponentItem> items;
        for (const auto& desc : InspectorAddableComponents()) {
            if (QLatin1String(desc.id) == QLatin1String("Environment")) continue;
            AddComponentItem item;
            item.id = QString::fromUtf8(desc.id);
            item.displayName = QString::fromUtf8(desc.displayName);
            item.category = QString::fromUtf8(desc.category);
            items.push_back(item);
        }
        auto* popup = new AddComponentPopup(this);
        popup->setAttribute(Qt::WA_DeleteOnClose);
        connect(popup, &AddComponentPopup::componentChosen, this, [this](const QString& typeId) {
            emit addComponentRequested(m_currentNodeId, typeId.toStdString());
        });
        popup->Present(QCursor::pos(), items);
    });
    m_contentLayout->addWidget(m_addComponentBtn);
    m_contentLayout->addStretch(1);

    m_scrollArea->setWidget(m_contentWidget);
    outerLayout->addWidget(m_scrollArea, 1);

    Clear();
}

void PrefabInspectorPanel::buildTransformGroup() {
    m_transformGroup = new QGroupBox(tr("Transform"), m_contentWidget);
    m_transformGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid #3a3a3a; "
                                    "border-radius: 3px; margin-top: 8px; padding-top: 14px; }");
    auto* tLayout = new QVBoxLayout(m_transformGroup);
    tLayout->setSpacing(2);

    tLayout->addWidget(makeVec3Row(tr("Pos"), m_posX, m_posY, m_posZ, m_transformGroup));
    tLayout->addWidget(makeVec3Row(tr("Rot"), m_rotX, m_rotY, m_rotZ, m_transformGroup));
    tLayout->addWidget(makeVec3Row(tr("Scl"), m_scaleX, m_scaleY, m_scaleZ, m_transformGroup));
    m_rotX->setSuffix(QString::fromUtf8("\xC2\xB0"));
    m_rotY->setSuffix(QString::fromUtf8("\xC2\xB0"));
    m_rotZ->setSuffix(QString::fromUtf8("\xC2\xB0"));

    auto emitTransform = [this]() {
        if (m_updatingUI || m_currentNodeId.empty()) return;
        TransformComponent t;
        t.position = { (float)m_posX->value(), (float)m_posY->value(), (float)m_posZ->value() };
        t.rotation = { (float)m_rotX->value() * kDegToRad,
                       (float)m_rotY->value() * kDegToRad,
                       (float)m_rotZ->value() * kDegToRad };
        t.scale = { (float)m_scaleX->value(), (float)m_scaleY->value(), (float)m_scaleZ->value() };
        emit transformChanged(m_currentNodeId, t);
    };

    for (auto* spin : { m_posX, m_posY, m_posZ, m_rotX, m_rotY, m_rotZ, m_scaleX, m_scaleY, m_scaleZ }) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitTransform);
    }
}

void PrefabInspectorPanel::ShowNode(const PrefabNode* node, Scene* /*previewScene*/,
                                     const std::string& /*rootNodeId*/) {
    ShowEntity(nullptr, nullptr, {}, node);
}

void PrefabInspectorPanel::ShowEntity(EditorContext* ctx, Scene* previewScene, Entity entity,
                                      const PrefabNode* node) {
    if (!node) {
        Clear();
        return;
    }

    m_updatingUI = true;
    m_currentNodeId = node->id;

    m_nodeGroup->setEnabled(true);
    m_nameEdit->setText(QString::fromStdString(node->name));

    m_transformGroup->setEnabled(true);
    blockTransformSignals(true);
    const TransformComponent& transform = (entity && entity.HasComponent<TransformComponent>())
        ? entity.GetComponent<TransformComponent>()
        : node->localTransform;
    m_posX->setValue(transform.position.x);
    m_posY->setValue(transform.position.y);
    m_posZ->setValue(transform.position.z);
    m_rotX->setValue(transform.rotation.x * kRadToDeg);
    m_rotY->setValue(transform.rotation.y * kRadToDeg);
    m_rotZ->setValue(transform.rotation.z * kRadToDeg);
    m_scaleX->setValue(transform.scale.x);
    m_scaleY->setValue(transform.scale.y);
    m_scaleZ->setValue(transform.scale.z);
    blockTransformSignals(false);

    if (ctx && previewScene && entity && ctx->GetActiveScene() == previewScene) {
        buildLiveComponentUI(ctx, previewScene, entity);
    } else {
        buildComponentUI(node);
    }

    m_addComponentBtn->setEnabled(true);
    m_updatingUI = false;
}

void PrefabInspectorPanel::Clear() {
    m_updatingUI = true;
    m_currentNodeId.clear();

    m_nameEdit->clear();
    m_nameEdit->setEnabled(false);
    m_nodeGroup->setEnabled(false);

    blockTransformSignals(true);
    m_transformGroup->setEnabled(false);
    m_posX->setValue(0); m_posY->setValue(0); m_posZ->setValue(0);
    m_rotX->setValue(0); m_rotY->setValue(0); m_rotZ->setValue(0);
    m_scaleX->setValue(1); m_scaleY->setValue(1); m_scaleZ->setValue(1);
    blockTransformSignals(false);

    clearComponentWidgets();
    m_addComponentBtn->setEnabled(false);
    m_updatingUI = false;
}

void PrefabInspectorPanel::RefreshTransform(const TransformComponent& transform) {
    m_updatingUI = true;
    blockTransformSignals(true);
    m_posX->setValue(transform.position.x);
    m_posY->setValue(transform.position.y);
    m_posZ->setValue(transform.position.z);
    m_rotX->setValue(transform.rotation.x * kRadToDeg);
    m_rotY->setValue(transform.rotation.y * kRadToDeg);
    m_rotZ->setValue(transform.rotation.z * kRadToDeg);
    m_scaleX->setValue(transform.scale.x);
    m_scaleY->setValue(transform.scale.y);
    m_scaleZ->setValue(transform.scale.z);
    blockTransformSignals(false);
    m_updatingUI = false;
}

void PrefabInspectorPanel::buildComponentUI(const PrefabNode* node) {
    clearComponentWidgets();
    for (const auto& entry : node->components) {
        auto* widget = createReadOnlyComponentWidget(entry, node->id);
        if (widget) {
            m_componentsLayout->addWidget(widget);
            m_componentWidgets.push_back(widget);
        }
    }
}

void PrefabInspectorPanel::buildLiveComponentUI(EditorContext* ctx, Scene* previewScene, Entity entity) {
    clearComponentWidgets();
    if (!ctx || !previewScene || !entity) {
        return;
    }

    auto& registry = previewScene->GetRegistry();
    const entt::entity handle = static_cast<entt::entity>(entity);

    auto inspectors = ComponentInspectorRegistry::Instance().CreateInspectors(
        ctx, handle, registry, m_componentsContainer);
    for (auto* insp : inspectors) {
        connect(insp, &ComponentInspector::componentRemoved, this,
                [this](entt::entity, const QString& name) {
                    emit removeComponentRequested(m_currentNodeId, name.toStdString());
                    emit previewComponentsChanged();
                });
        m_componentsLayout->addWidget(insp);
        m_componentWidgets.push_back(insp);
    }
}

QWidget* PrefabInspectorPanel::createReadOnlyComponentWidget(const PrefabComponentEntry& entry,
                                                             const std::string& nodeId) {
    auto* group = new QGroupBox(QString::fromStdString(entry.type), m_componentsContainer);
    auto* layout = new QVBoxLayout(group);
    auto* removeBtn = new QPushButton(tr("Remove"), group);
    connect(removeBtn, &QPushButton::clicked, this, [this, nodeId, type = entry.type]() {
        emit removeComponentRequested(nodeId, type);
    });
    layout->addWidget(removeBtn);
    return group;
}

void PrefabInspectorPanel::clearComponentWidgets() {
    for (auto* w : m_componentWidgets) {
        m_componentsLayout->removeWidget(w);
        delete w;
    }
    m_componentWidgets.clear();
}

void PrefabInspectorPanel::blockTransformSignals(bool block) {
    for (auto* spin : { m_posX, m_posY, m_posZ, m_rotX, m_rotY, m_rotZ, m_scaleX, m_scaleY, m_scaleZ }) {
        spin->blockSignals(block);
    }
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
