#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "ComponentInspector.h"
#include "PropertyEditors.h"
#include "AssetReferenceEditor.h"

namespace Hephaestus {
namespace QtEditor {

class AudioSourceInspector : public ComponentInspector
{
    Q_OBJECT
public:
    explicit AudioSourceInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    void Refresh() override;

private:
    void buildUI();
    BoolPropertyEditor* m_enabled = nullptr;
    BoolPropertyEditor* m_playOnStart = nullptr;
    BoolPropertyEditor* m_loop = nullptr;
    NumericPropertyEditor* m_volume = nullptr;
    AssetReferenceEditor* m_clip = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
