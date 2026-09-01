/**
 * @file AssetDependencyDialog.cpp
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "AssetDependencyDialog.h"

#include "../../Assets/AssetReferenceIndex.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QPushButton>

namespace Hephaestus::QtEditor {

AssetDependencyDialog::AssetDependencyDialog(AssetReferenceIndex* index,
                                             const QString& assetPortablePath,
                                             QWidget* parent)
    : QDialog(parent)
    , m_index(index)
    , m_assetPath(assetPortablePath)
{
    setWindowTitle(tr("Asset References"));
    setMinimumSize(480, 360);
    resize(560, 420);

    auto* layout = new QVBoxLayout(this);

    m_headerLabel = new QLabel(this);
    m_headerLabel->setWordWrap(true);
    m_headerLabel->setText(tr("References for:\n%1").arg(m_assetPath));
    layout->addWidget(m_headerLabel);

    m_tabs = new QTabWidget(this);
    m_dependenciesList = new QListWidget(m_tabs);
    m_referrersList = new QListWidget(m_tabs);
    m_tabs->addTab(m_dependenciesList, tr("Dependencies"));
    m_tabs->addTab(m_referrersList, tr("Used By"));
    layout->addWidget(m_tabs, 1);

    auto openOnActivate = [this](QListWidgetItem* item) {
        if (!item) {
            return;
        }
        onItemActivated(item->data(Qt::UserRole).toString());
    };
    connect(m_dependenciesList, &QListWidget::itemDoubleClicked, this, openOnActivate);
    connect(m_referrersList, &QListWidget::itemDoubleClicked, this, openOnActivate);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(buttons);

    rebuildLists();
}

void AssetDependencyDialog::SetOpenOrRevealCallback(OpenOrRevealCallback callback) {
    m_openCallback = std::move(callback);
}

void AssetDependencyDialog::rebuildLists() {
    m_dependenciesList->clear();
    m_referrersList->clear();
    if (!m_index || m_assetPath.isEmpty()) {
        return;
    }

    const std::string key = m_assetPath.toStdString();
    const auto deps = m_index->GetDependencies(key);
    for (const auto& ref : deps) {
        const QString path = QString::fromStdString(ref.referencedPath);
        QString label = path;
        if (ref.isAdvisory) {
            label += tr(" (advisory)");
        }
        label += QStringLiteral("  [%1]").arg(QString::fromUtf8(ToString(ref.fieldKind)));
        auto* item = new QListWidgetItem(label, m_dependenciesList);
        item->setData(Qt::UserRole, path);
        item->setToolTip(QString::fromStdString(ref.structuredLocation));
    }
    if (deps.empty()) {
        auto* empty = new QListWidgetItem(tr("(none)"), m_dependenciesList);
        empty->setFlags(empty->flags() & ~Qt::ItemIsEnabled);
    }

    const auto referrers = m_index->GetReferrers(key);
    for (const auto& ref : referrers) {
        const QString path = QString::fromStdString(ref.documentPath);
        QString label = path;
        label += QStringLiteral("  [%1]").arg(QString::fromUtf8(ToString(ref.fieldKind)));
        auto* item = new QListWidgetItem(label, m_referrersList);
        item->setData(Qt::UserRole, path);
        item->setToolTip(QString::fromStdString(ref.structuredLocation));
    }
    if (referrers.empty()) {
        auto* empty = new QListWidgetItem(tr("(none)"), m_referrersList);
        empty->setFlags(empty->flags() & ~Qt::ItemIsEnabled);
    }

    m_tabs->setTabText(0, tr("Dependencies (%1)").arg(static_cast<int>(deps.size())));
    m_tabs->setTabText(1, tr("Used By (%1)").arg(static_cast<int>(referrers.size())));
}

void AssetDependencyDialog::onItemActivated(const QString& portablePath) {
    if (portablePath.isEmpty() || !m_openCallback) {
        return;
    }
    m_openCallback(portablePath);
}

} // namespace Hephaestus::QtEditor

#endif // HEPHAESTUS_EDITOR_QT_UI
