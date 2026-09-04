#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "../../Animation/AnimationClip.h"
#include "../../Assets/CharacterImport.h"

#include <QDialog>
#include <QString>
#include <string>
#include <vector>

class QTableWidget;
class QComboBox;

namespace Hephaestus {
namespace QtEditor {

class CharacterAnimationImportDialog : public QDialog
{
    Q_OBJECT
public:
    CharacterAnimationImportDialog(const std::vector<AnimationClip>& clips,
                                   const std::vector<std::string>& folders,
                                   const QString& sourceStem,
                                   QWidget* parent = nullptr);

    std::vector<CharacterClipImportChoice> selectedClips() const;
    QString destinationFolder() const;

private:
    QTableWidget* m_table = nullptr;
    QComboBox* m_folder = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
