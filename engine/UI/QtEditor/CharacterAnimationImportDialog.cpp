#if HEPHAESTUS_EDITOR_QT_UI

#include "CharacterAnimationImportDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>

namespace Hephaestus {
namespace QtEditor {

CharacterAnimationImportDialog::CharacterAnimationImportDialog(const std::vector<AnimationClip>& clips,
                                                               const std::vector<std::string>& folders,
                                                               QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Animations Found"));
    resize(420, 360);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Select clips to import:"), this));
    m_list = new QListWidget(this);
    for (const AnimationClip& clip : clips) {
        const QString name = QString::fromStdString(clip.sourceClip.empty() ? clip.name : clip.sourceClip);
        auto* item = new QListWidgetItem(name, m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, name);
    }
    layout->addWidget(m_list, 1);
    layout->addWidget(new QLabel(tr("Destination Folder:"), this));
    m_folder = new QComboBox(this);
    m_folder->setEditable(true);
    m_folder->addItem(QString());
    for (const std::string& folder : folders) {
        m_folder->addItem(QString::fromStdString(folder));
    }
    layout->addWidget(m_folder);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

std::vector<std::string> CharacterAnimationImportDialog::selectedSourceClips() const {
    std::vector<std::string> selected;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* item = m_list->item(i);
        if (item->checkState() == Qt::Checked) {
            selected.push_back(item->data(Qt::UserRole).toString().toStdString());
        }
    }
    return selected;
}

QString CharacterAnimationImportDialog::destinationFolder() const {
    return m_folder->currentText().trimmed();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
