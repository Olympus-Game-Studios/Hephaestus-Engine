#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "ComponentInspector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "../../Construction/ConstructionShape.h"

#include <functional>
#include <vector>

namespace Hephaestus::Construction {
class ConstructionSystem;
}

namespace Hephaestus {
namespace QtEditor {

class ConstructionShapeInspector : public ComponentInspector
{
    Q_OBJECT

public:
    ConstructionShapeInspector(EditorContext* ctx, entt::entity entity,
                               Hephaestus::Construction::ConstructionSystem* system, QWidget* parent = nullptr);
    void Refresh() override;

private:
    void buildUI();
    void connectSignals();
    void applyToComponent();
    void rebuildFaceList();
    void refreshKit(Hephaestus::Construction::Recipe recipe, bool hollow);
    void refreshPickedPiece(const ConstructionShapeComponent& shape);
    void swapPickedPiece(int index);

    QLabel* m_kind = nullptr;
    QLineEdit* m_role = nullptr;
    QComboBox* m_operation = nullptr;
    QCheckBox* m_solid = nullptr;

    QGroupBox* m_boxGroup = nullptr;
    QFormLayout* m_boxForm = nullptr;
    QFormLayout* m_generalForm = nullptr;
    QDoubleSpinBox* m_width = nullptr;
    QDoubleSpinBox* m_height = nullptr;
    QDoubleSpinBox* m_depth = nullptr;
    QDoubleSpinBox* m_segments = nullptr;
    QCheckBox* m_hollow = nullptr;
    QCheckBox* m_closed = nullptr;
    QLabel* m_wallInfo = nullptr;
    QDoubleSpinBox* m_wallThickness = nullptr;
    QCheckBox* m_floor = nullptr;
    QCheckBox* m_ceiling = nullptr;

    QGroupBox* m_stairsGroup = nullptr;
    QDoubleSpinBox* m_steps = nullptr;
    QDoubleSpinBox* m_maxRise = nullptr;
    QLabel* m_stairsInfo = nullptr;
    QFormLayout* m_stairsForm = nullptr;
    QComboBox* m_turn = nullptr;
    QDoubleSpinBox* m_flightWidth = nullptr;
    QLabel* m_rampInfo = nullptr;

    QGroupBox* m_openingGroup = nullptr;
    QFormLayout* m_openingForm = nullptr;
    QComboBox* m_side = nullptr;
    QDoubleSpinBox* m_openingSegment = nullptr;
    QDoubleSpinBox* m_offset = nullptr;
    QDoubleSpinBox* m_sill = nullptr;
    QDoubleSpinBox* m_openingWidth = nullptr;
    QDoubleSpinBox* m_openingHeight = nullptr;

    QGroupBox* m_facesGroup = nullptr;
    QVBoxLayout* m_facesList = nullptr;
    std::vector<Hephaestus::Construction::FaceMaterial> m_shownFaces;
    // Texture alignment fields of each listed face, updated in place when only the alignment changed.
    struct FaceAlignmentRow {
        QDoubleSpinBox* offsetU = nullptr;
        QDoubleSpinBox* offsetV = nullptr;
        QDoubleSpinBox* scaleU = nullptr;
        QDoubleSpinBox* scaleV = nullptr;
        QDoubleSpinBox* rotation = nullptr;
        QCheckBox* fit = nullptr;
    };
    std::vector<FaceAlignmentRow> m_faceRows;
    void showFaceAlignment(const FaceAlignmentRow& row, const Hephaestus::Construction::FaceMaterial& face);

    QGroupBox* m_kitGroup = nullptr;
    QComboBox* m_kit = nullptr;
    QLabel* m_kitInfo = nullptr;
    QPushButton* m_kitAccept = nullptr;
    QPushButton* m_kitCancel = nullptr;
    // Shows or clears a kit preview on every selected shape a kit can dress, and updates the shapes now.
    void previewKit(const std::string& kit);
    QLabel* m_piece = nullptr;
    QComboBox* m_pieceSwap = nullptr;
    QPushButton* m_clearSwaps = nullptr;
    std::string m_pickedSlot;
    Hephaestus::Construction::ConstructionSystem* m_system = nullptr;

    QPushButton* m_remove = nullptr;
    QPushButton* m_makeEditable = nullptr;
    QPushButton* m_revertEditable = nullptr;
    QPushButton* m_addStorey = nullptr;
    QPushButton* m_addStairs = nullptr;
    QPushButton* m_flatRoof = nullptr;
    QPushButton* m_pitchedRoof = nullptr;
    QPushButton* m_addBalcony = nullptr;
    void runBuildingAction(const char* label, const std::function<entt::entity(Scene&, std::string&)>& action,
                           const QString& done);
    QLabel* m_storeyInfo = nullptr;
    QLabel* m_meshInfo = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
