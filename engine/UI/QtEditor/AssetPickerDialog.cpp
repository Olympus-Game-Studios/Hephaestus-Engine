#if HEPHAESTUS_EDITOR_QT_UI

#include "AssetPickerDialog.h"

#include "../../Assets/AssetManager.h"
#include "../../Assets/AssetPathPolicy.h"

#include <QDialogButtonBox>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <algorithm>
#include <filesystem>

namespace Hephaestus {
namespace QtEditor {

AssetPickerDialog::AssetPickerDialog(AssetType type, QWidget* parent)
    : QDialog(parent)
    , m_type(type)
{
    setWindowTitle(tr("Select %1").arg(QString::fromUtf8(ToString(type))));
    resize(420, 480);
    auto* layout = new QVBoxLayout(this);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search…"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);
    m_list = new QListWidget(this);
    layout->addWidget(m_list, 1);
    m_preview = new QLabel(this);
    m_preview->setWordWrap(true);
    m_preview->setStyleSheet("color: #9DA3AE;");
    layout->addWidget(m_preview);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset, this);
    buttons->button(QDialogButtonBox::Reset)->setText(tr("Clear"));
    layout->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, this, [this]() { rebuildList(); });
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() {
        auto* item = m_list->currentItem();
        m_preview->setText(item ? item->data(Qt::UserRole).toString() : QString());
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!item) return;
        m_selected = item->data(Qt::UserRole).toString();
        accept();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        auto* item = m_list->currentItem();
        if (item) {
            m_selected = item->data(Qt::UserRole).toString();
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this]() {
        m_selected = QString();
        accept();
    });
    rebuildList();
}

void AssetPickerDialog::rebuildList()
{
    m_list->clear();
    const QString query = m_search->text().trimmed().toLower();
    auto assets = AssetManager::GetInstance().GetAssetsByType(m_type);
    std::sort(assets.begin(), assets.end(), [](const AssetInfo& a, const AssetInfo& b) {
        return a.name < b.name;
    });
    for (const auto& asset : assets) {
        const QString name = QString::fromStdString(asset.name);
        std::string portable = Hephaestus::AssetPathPolicy::ToPortableKey(
            std::filesystem::path(AssetManager::GetInstance().GetProjectRoot()),
            asset.absolutePath);
        if (portable.empty()) {
            portable = asset.relativePath;
            if (portable.rfind("assets/", 0) != 0 && portable.rfind(".engine/", 0) != 0) {
                portable = "assets/" + portable;
            }
        }
        const QString path = QString::fromStdString(portable);
        if (!query.isEmpty() &&
            !name.toLower().contains(query) &&
            !path.toLower().contains(query)) {
            continue;
        }
        auto* item = new QListWidgetItem(name, m_list);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
    }
}

std::optional<QString> AssetPickerDialog::Choose(AssetType type, QWidget* parent,
                                                 const QString& currentPath)
{
    AssetPickerDialog dialog(type, parent);
    if (!currentPath.isEmpty()) {
        for (int i = 0; i < dialog.m_list->count(); ++i) {
            if (dialog.m_list->item(i)->data(Qt::UserRole).toString().compare(
                    currentPath, Qt::CaseInsensitive) == 0) {
                dialog.m_list->setCurrentRow(i);
                break;
            }
        }
    }
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return dialog.m_selected;
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
