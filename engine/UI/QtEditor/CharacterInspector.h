#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "AssetReferenceEditor.h"
#include "ComponentInspector.h"
#include "PropertyEditors.h"
#include "../../Assets/CharacterAsset.h"
#include "../../Scene/CharacterComponent.h"
#include "../../Scripting/LuaExposeMetadata.h"

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>
#include <filesystem>
#include <vector>

namespace Hephaestus {
namespace QtEditor {

class CharacterInspector : public ComponentInspector
{
    Q_OBJECT
public:
    explicit CharacterInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    void Refresh() override;

private:
    void buildUI();
    void applyCharacterPath(const QString& path);
    void rebindTargets(bool applyAssetDefaults);
    void rebuildClipSelector(const CharacterAsset* asset, const std::string& currentId);
    void rebuildMaterialSlots(const CharacterAsset* asset, const CharacterComponent& character);
    void rebuildControllerVariables(const std::string& scriptPath);
    CharacterAsset loadAsset(const std::string& portable, std::string& error) const;
    std::filesystem::path projectRoot() const;

    AssetReferenceEditor* m_assetEdit = nullptr;
    QComboBox* m_clipCombo = nullptr;
    QLabel* m_clipHint = nullptr;
    NumericPropertyEditor* m_speed = nullptr;
    BoolPropertyEditor* m_runController = nullptr;
    AssetReferenceEditor* m_controllerEdit = nullptr;
    QLabel* m_controllerNote = nullptr;
    QGroupBox* m_materialsGroup = nullptr;
    QVBoxLayout* m_materialsLayout = nullptr;
    QGroupBox* m_parametersGroup = nullptr;
    QVBoxLayout* m_parametersLayout = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_advanced = nullptr;
    std::vector<std::string> m_slotNames;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
