#pragma once

/**
 * @file LightInspector.h
 * @brief Inspector widget for the LightComponent in the Details panel.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "ComponentInspector.h"
#include "PropertyEditors.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QWidget>

namespace Hephaestus {

class EditorContext;

namespace QtEditor {

class LightInspector : public ComponentInspector
{
    Q_OBJECT

public:
    explicit LightInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    ~LightInspector() override = default;

    void Refresh() override;

private:
    void buildUI();
    void connectSignals();
    void updateTypeVisibility();
    void blockAllSignals(bool block);
    void writeIntensity(double value);
    void pickColor();

    BoolPropertyEditor* m_enabled = nullptr;
    EnumPropertyEditor* m_type = nullptr;
    ColorPropertyEditor* m_color = nullptr;
    NumericPropertyEditor* m_intensity = nullptr;
    NumericPropertyEditor* m_range = nullptr;
    NumericPropertyEditor* m_innerCone = nullptr;
    NumericPropertyEditor* m_outerCone = nullptr;
    QWidget* m_rangeRow = nullptr;
    QWidget* m_innerConeRow = nullptr;
    QWidget* m_outerConeRow = nullptr;
    BoolPropertyEditor* m_castShadows = nullptr;
    BoolPropertyEditor* m_shadowAutoFit = nullptr;
    QWidget* m_shadowGroup = nullptr;
    NumericPropertyEditor* m_shadowBias = nullptr;
    NumericPropertyEditor* m_shadowNormalBias = nullptr;
    NumericPropertyEditor* m_shadowStrength = nullptr;
    NumericPropertyEditor* m_sunSize = nullptr;
    QWidget* m_shadowManualGroup = nullptr;
    NumericPropertyEditor* m_shadowDistance = nullptr;
    NumericPropertyEditor* m_shadowExtent = nullptr;
    NumericPropertyEditor* m_shadowMaxDistance = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
