#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "../../Animation/AnimationClip.h"

#include <QDialog>
#include <QString>
#include <filesystem>
#include <string>
#include <vector>

class QListWidget;
class QComboBox;

namespace Hephaestus {
namespace QtEditor {

class CharacterAnimationImportDialog : public QDialog
{
    Q_OBJECT
public:
    CharacterAnimationImportDialog(const std::vector<AnimationClip>& clips,
                                   const std::vector<std::string>& folders,
                                   QWidget* parent = nullptr);

    std::vector<std::string> selectedSourceClips() const;
    QString destinationFolder() const;

private:
    QListWidget* m_list = nullptr;
    QComboBox* m_folder = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
