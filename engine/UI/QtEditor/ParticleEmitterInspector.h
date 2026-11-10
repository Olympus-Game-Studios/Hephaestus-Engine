#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "ComponentInspector.h"
#include "PropertyEditors.h"
#include "AssetReferenceEditor.h"

#include "../../Scene/Components.h"

#include <vector>

namespace Hephaestus {
namespace QtEditor {

class ParticleEmitterInspector : public ComponentInspector
{
    Q_OBJECT
public:
    explicit ParticleEmitterInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    void Refresh() override;

private:
    struct NumberField {
        NumericPropertyEditor* editor = nullptr;
        float ParticleEmitterComponent::* member = nullptr;
        int ParticleEmitterComponent::* intMember = nullptr;
    };

    void buildUI();
    AssetReferenceEditor* m_material = nullptr;
    BoolPropertyEditor* m_playOnStart = nullptr;
    BoolPropertyEditor* m_loop = nullptr;
    BoolPropertyEditor* m_destroyWhenDone = nullptr;
    ColorRgbaPropertyEditor* m_colorStart = nullptr;
    ColorRgbaPropertyEditor* m_colorEnd = nullptr;
    std::vector<NumberField> m_numbers;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
