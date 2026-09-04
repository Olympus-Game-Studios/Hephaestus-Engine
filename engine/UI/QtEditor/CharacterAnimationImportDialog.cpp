#if HEPHAESTUS_EDITOR_QT_UI

#include "CharacterAnimationImportDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace Hephaestus {
namespace QtEditor {
namespace {

bool IsGenericTakeName(const QString& name) {
    const QString lower = name.trimmed().toLower();
    if (lower.isEmpty()) {
        return true;
    }
    if (lower == QLatin1String("mixamo.com") || lower.endsWith(QLatin1String("|mixamo.com")) ||
        lower.endsWith(QLatin1String("/mixamo.com"))) {
        return true;
    }
    if (lower.startsWith(QLatin1String("take"))) {
        QString rest = name.trimmed().mid(4);
        rest.remove(QLatin1Char(' '));
        bool ok = false;
        rest.toInt(&ok);
        return ok;
    }
    return false;
}

bool IsMixamoTake(const QString& name) {
    const QString lower = name.trimmed().toLower();
    return lower == QLatin1String("mixamo.com") || lower.endsWith(QLatin1String("|mixamo.com")) ||
           lower.endsWith(QLatin1String("/mixamo.com"));
}

QString SourceTakeName(const AnimationClip& clip) {
    return QString::fromStdString(clip.sourceClip.empty() ? clip.name : clip.sourceClip);
}

} // namespace

CharacterAnimationImportDialog::CharacterAnimationImportDialog(const std::vector<AnimationClip>& clips,
                                                               const std::vector<std::string>& folders,
                                                               const QString& sourceStem,
                                                               QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Animations Found"));
    resize(560, 400);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        tr("Select clips to import. Name becomes the clip id used by playback and controllers."),
        this));
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Import"), tr("Source"), tr("Name")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                             QAbstractItemView::SelectedClicked);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setRowCount(static_cast<int>(clips.size()));

    bool hasMixamoTake = false;
    for (const AnimationClip& clip : clips) {
        if (IsMixamoTake(SourceTakeName(clip))) {
            hasMixamoTake = true;
            break;
        }
    }

    QSet<QString> usedNames;
    for (int row = 0; row < static_cast<int>(clips.size()); ++row) {
        const QString sourceTake = SourceTakeName(clips[static_cast<size_t>(row)]);
        auto* importItem = new QTableWidgetItem();
        importItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        const bool restTake = hasMixamoTake && IsGenericTakeName(sourceTake) && !IsMixamoTake(sourceTake);
        importItem->setCheckState(restTake ? Qt::Unchecked : Qt::Checked);
        m_table->setItem(row, 0, importItem);

        auto* sourceItem = new QTableWidgetItem(sourceTake);
        sourceItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, 1, sourceItem);

        QString suggested = sourceTake;
        if (!sourceStem.trimmed().isEmpty() && IsGenericTakeName(sourceTake)) {
            suggested = sourceStem.trimmed();
        }
        if (usedNames.contains(suggested)) {
            suggested = sourceStem.trimmed().isEmpty() ? sourceTake
                                                       : sourceStem.trimmed() + " (" + sourceTake + ")";
        }
        usedNames.insert(suggested);
        auto* nameItem = new QTableWidgetItem(suggested);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        m_table->setItem(row, 2, nameItem);
    }
    layout->addWidget(m_table, 1);
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

std::vector<CharacterClipImportChoice> CharacterAnimationImportDialog::selectedClips() const {
    std::vector<CharacterClipImportChoice> selected;
    if (!m_table) {
        return selected;
    }
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem* importItem = m_table->item(row, 0);
        QTableWidgetItem* sourceItem = m_table->item(row, 1);
        QTableWidgetItem* nameItem = m_table->item(row, 2);
        if (!importItem || importItem->checkState() != Qt::Checked || !sourceItem) {
            continue;
        }
        CharacterClipImportChoice choice;
        choice.sourceClip = sourceItem->text().toStdString();
        choice.name = nameItem ? nameItem->text().trimmed().toStdString() : std::string{};
        selected.push_back(std::move(choice));
    }
    return selected;
}

QString CharacterAnimationImportDialog::destinationFolder() const {
    return m_folder->currentText().trimmed();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
