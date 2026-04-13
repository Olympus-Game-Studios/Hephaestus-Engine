#if HEPHAESTUS_EDITOR_QT_UI

#include "AboutDialog.h"
#include "../../ToolsCommon/VersionInfo.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QApplication>
#include <QStyle>
#include <QFont>
#include <QDesktopServices>
#include <QUrl>

namespace Hephaestus::QtEditor {

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    using namespace Hephaestus::Tools::VersionInfo;

    setWindowTitle(tr("About %1").arg(AppName()));
    setFixedSize(440, 320);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 16);
    root->setSpacing(8);

    // --- Icon + title row ---
    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(14);

    auto* iconLabel = new QLabel(this);
    QPixmap appPixmap = QApplication::style()->standardPixmap(QStyle::SP_ComputerIcon);
    iconLabel->setPixmap(appPixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    iconLabel->setFixedSize(64, 64);
    topRow->addWidget(iconLabel, 0, Qt::AlignTop);

    auto* titleBlock = new QVBoxLayout;
    titleBlock->setSpacing(2);

    auto* nameLabel = new QLabel(AppName(), this);
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(16);
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    titleBlock->addWidget(nameLabel);

    auto* versionLabel = new QLabel(tr("Version %1").arg(EngineVersion()), this);
    QFont verFont = versionLabel->font();
    verFont.setPointSize(10);
    versionLabel->setFont(verFont);
    versionLabel->setStyleSheet(QStringLiteral("color: #888;"));
    titleBlock->addWidget(versionLabel);

    topRow->addLayout(titleBlock, 1);
    root->addLayout(topRow);

    root->addSpacing(8);

    // --- Description ---
    auto* descLabel = new QLabel(AppDescription(), this);
    descLabel->setWordWrap(true);
    root->addWidget(descLabel);

    // --- Tech stack ---
    auto* techLabel = new QLabel(
        tr("Built with C++20 \u00b7 Vulkan \u00b7 Qt %1 \u00b7 Jolt Physics \u00b7 Lua 5.4")
            .arg(QLatin1String(qVersion())),
        this);
    techLabel->setWordWrap(true);
    techLabel->setStyleSheet(QStringLiteral("color: #888;"));
    root->addWidget(techLabel);

    root->addSpacing(4);

    // --- Copyright ---
    auto* copyrightLabel = new QLabel(Copyright(), this);
    copyrightLabel->setStyleSheet(QStringLiteral("color: #888;"));
    root->addWidget(copyrightLabel);

    // --- Website link ---
    auto* linkLabel = new QLabel(
        QStringLiteral("<a href=\"%1\">%1</a>").arg(Website()), this);
    linkLabel->setOpenExternalLinks(true);
    linkLabel->setStyleSheet(QStringLiteral("color: #5599dd;"));
    root->addWidget(linkLabel);

    root->addStretch(1);

    // --- Close button ---
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);
}

} // namespace Hephaestus::QtEditor

#endif // HEPHAESTUS_EDITOR_QT_UI
