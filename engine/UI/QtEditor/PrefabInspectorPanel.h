#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QMenu>
#include <functional>
#include <string>
#include <vector>

#include "../../Scene/PrefabAsset.h"
#include "../../Scene/Scene.h"
#include "../../Scene/Components.h"

namespace Hephaestus {

class EditorContext;

namespace QtEditor {

class ComponentInspector;
class LightInspector;

class PrefabInspectorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PrefabInspectorPanel(QWidget* parent = nullptr);

    void ShowNode(const PrefabNode* node, Scene* previewScene, const std::string& rootNodeId);
    void ShowEntity(EditorContext* ctx, Scene* previewScene, Entity entity, const PrefabNode* node);
    void Clear();
    void RefreshTransform(const TransformComponent& transform);

signals:
    void nameChanged(const std::string& nodeId, const std::string& newName);
    void transformChanged(const std::string& nodeId, const TransformComponent& transform);
    void addComponentRequested(const std::string& nodeId, const std::string& componentType);
    void removeComponentRequested(const std::string& nodeId, const std::string& componentType);
    void previewComponentsChanged();

private:
    void buildTransformGroup();
    void buildComponentUI(const PrefabNode* node);
    void buildLiveComponentUI(EditorContext* ctx, Scene* previewScene, Entity entity);
    QWidget* createReadOnlyComponentWidget(const PrefabComponentEntry& entry, const std::string& nodeId);
    void clearComponentWidgets();
    void blockTransformSignals(bool block);

    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;

    QLabel* m_headerLabel = nullptr;
    QGroupBox* m_nodeGroup = nullptr;
    QLineEdit* m_nameEdit = nullptr;

    QGroupBox* m_transformGroup = nullptr;
    QDoubleSpinBox* m_posX = nullptr;
    QDoubleSpinBox* m_posY = nullptr;
    QDoubleSpinBox* m_posZ = nullptr;
    QDoubleSpinBox* m_rotX = nullptr;
    QDoubleSpinBox* m_rotY = nullptr;
    QDoubleSpinBox* m_rotZ = nullptr;
    QDoubleSpinBox* m_scaleX = nullptr;
    QDoubleSpinBox* m_scaleY = nullptr;
    QDoubleSpinBox* m_scaleZ = nullptr;

    QWidget* m_componentsContainer = nullptr;
    QVBoxLayout* m_componentsLayout = nullptr;
    QPushButton* m_addComponentBtn = nullptr;
    std::vector<QWidget*> m_componentWidgets;
    LightInspector* m_lightInspector = nullptr;

    std::string m_currentNodeId;
    bool m_updatingUI = false;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
