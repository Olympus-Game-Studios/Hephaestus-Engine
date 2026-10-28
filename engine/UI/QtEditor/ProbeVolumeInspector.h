#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "ComponentInspector.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>

#include <cstdint>
#include <functional>

namespace Hephaestus {
namespace QtEditor {

class ProbeVolumeInspector : public ComponentInspector
{
    Q_OBJECT

public:
    explicit ProbeVolumeInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    void Refresh() override;
    // Supplies the current static-geometry signature (0 = unknown) so the status can flag a stale bake.
    void SetGeometrySignatureProvider(std::function<uint64_t()> provider);

signals:
    void bakeRequested();

private:
    void buildUI();
    void connectSignals();
    void applyToComponent();

    QCheckBox* m_enabled = nullptr;
    QDoubleSpinBox* m_size[3] = {};
    QDoubleSpinBox* m_counts[3] = {};
    QDoubleSpinBox* m_intensity = nullptr;
    QLabel* m_total = nullptr;
    QLabel* m_bakeStatus = nullptr;
    QPushButton* m_bake = nullptr;
    QPushButton* m_remove = nullptr;
    std::function<uint64_t()> m_geometrySignature;
    bool m_updating = false;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
