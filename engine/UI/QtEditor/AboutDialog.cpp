#if HEPHAESTUS_EDITOR_QT_UI

#include "AboutDialog.h"
#include "EditorTheme.h"
#include "../../ToolsCommon/VersionInfo.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadialGradient>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace Hephaestus::QtEditor {

namespace {

struct Credit
{
    const char* group;
    const char* name;
    const char* role;
    const char* license;
    const char* url;
};

const Credit kCredits[] = {
    { QT_TRANSLATE_NOOP("AboutDialog", "Libraries"), "Qt", "Editor and launcher UI", "LGPL v3", "https://www.qt.io" },
    { QT_TRANSLATE_NOOP("AboutDialog", "Libraries"), "Vulkan Memory Allocator", "GPU memory", "MIT", "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator" },
    { QT_TRANSLATE_NOOP("AboutDialog", "Libraries"), "Jolt Physics", "Physics", "MIT", "https://github.com/jrouwe/JoltPhysics" },
    { QT_TRANSLATE_NOOP("AboutDialog", "Libraries"), "EnTT", "Entity component system", "MIT", "https://github.com/skypjack/entt" },
    { QT_TRANSLATE_NOOP("AboutDialog", "Libraries"), "Lua", "Scripting", "MIT", "https://www.lua.org" },
    { QT_TRANSLATE_NOOP("AboutDialog", "Libraries"), "GLFW", "Game windows and input", "zlib", "https://www.glfw.org" },
    { QT_TRANSLATE_NOOP("AboutDialog", "Libraries"), "nlohmann/json", "JSON", "MIT", "https://github.com/nlohmann/json" },
    { QT_TRANSLATE_NOOP("AboutDialog", "Libraries"), "cpp-httplib", "Automation server", "MIT", "https://github.com/yhirose/cpp-httplib" },
    { QT_TRANSLATE_NOOP("AboutDialog", "Libraries"), "ufbx", "FBX import", "MIT / public domain", "https://github.com/ufbx/ufbx" },
    { QT_TRANSLATE_NOOP("AboutDialog", "Libraries"), "stb", "Images and font rasterising", "MIT / public domain", "https://github.com/nothings/stb" },
    { QT_TRANSLATE_NOOP("AboutDialog", "Fonts and icons"), "Inter", "Interface typeface", "OFL 1.1", "https://rsms.me/inter" },
    { QT_TRANSLATE_NOOP("AboutDialog", "Fonts and icons"), "JetBrains Mono", "Code typeface", "OFL 1.1", "https://www.jetbrains.com/lp/mono" },
    { QT_TRANSLATE_NOOP("AboutDialog", "Fonts and icons"), "Font Awesome Free", "Icon font", "OFL 1.1", "https://fontawesome.com" },
    { QT_TRANSLATE_NOOP("AboutDialog", "Fonts and icons"), "Material Icon Theme", "Code file icons", "MIT", "https://github.com/material-extensions/vscode-material-icon-theme" },
};

class AboutHero final : public QWidget
{
public:
    explicit AboutHero(QWidget* parent) : QWidget(parent) { setFixedHeight(150); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), Theme::Color(Theme::Token::SurfaceBase));
        QColor glow = Theme::Color(Theme::Token::AccentBrand);
        QRadialGradient gradient(QPointF(width() / 2.0, height() * 0.55), width() * 0.45);
        glow.setAlpha(70);
        gradient.setColorAt(0.0, glow);
        glow.setAlpha(0);
        gradient.setColorAt(1.0, glow);
        painter.fillRect(rect(), gradient);
        painter.setPen(Theme::Color(Theme::Token::BorderSubtle));
        painter.drawLine(rect().bottomLeft(), rect().bottomRight());
        const int logoSize = 88;
        Theme::BrandLogo().paint(&painter, QRect((width() - logoSize) / 2, (height() - logoSize) / 2, logoSize, logoSize));
    }
};

class CreditCard final : public QFrame
{
public:
    CreditCard(const Credit& credit, QWidget* parent)
        : QFrame(parent)
        , m_url(QString::fromLatin1(credit.url))
    {
        setObjectName(QStringLiteral("CreditCard"));
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setToolTip(m_url);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 8, 12, 8);
        layout->setSpacing(10);
        auto* text = new QVBoxLayout;
        text->setSpacing(1);
        auto* name = new QLabel(QString::fromUtf8(credit.name), this);
        name->setObjectName(QStringLiteral("CreditName"));
        text->addWidget(name);
        auto* role = new QLabel(QString::fromUtf8(credit.role), this);
        role->setProperty("role", "muted");
        text->addWidget(role);
        layout->addLayout(text, 1);
        auto* license = new QLabel(QString::fromLatin1(credit.license), this);
        license->setObjectName(QStringLiteral("CreditLicense"));
        layout->addWidget(license, 0, Qt::AlignVCenter);
    }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
            QDesktopServices::openUrl(QUrl(m_url));
        }
    }

private:
    QString m_url;
};

QLabel* CenteredLabel(const QString& text, const char* objectName, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QLatin1String(objectName));
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    return label;
}

QWidget* BuildOverview(QWidget* parent)
{
    using namespace Hephaestus::Tools::VersionInfo;
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 8, 32, 8);
    layout->setSpacing(14);
    layout->addWidget(CenteredLabel(AppDescription(), "AboutBlurb", page));

    auto* chips = new QHBoxLayout;
    chips->setSpacing(6);
    chips->addStretch(1);
    for (const QString& tech : { QStringLiteral("C++20"), QStringLiteral("Vulkan"),
                                 QStringLiteral("Qt %1").arg(QLatin1String(qVersion())),
                                 QStringLiteral("Jolt"), QStringLiteral("Lua 5.4") }) {
        auto* chip = new QLabel(tech, page);
        chip->setObjectName(QStringLiteral("AboutChip"));
        chips->addWidget(chip);
    }
    chips->addStretch(1);
    layout->addLayout(chips);
    layout->addStretch(1);

    auto* website = CenteredLabel(QStringLiteral("<a href=\"%1\">%2</a>")
                                      .arg(Website(), QString(Website()).remove(QStringLiteral("https://"))),
                                  "AboutLink", page);
    website->setOpenExternalLinks(true);
    layout->addWidget(website);
    layout->addWidget(CenteredLabel(Copyright(), "AboutFinePrint", page));
    return page;
}

QWidget* BuildCredits(QWidget* parent)
{
    auto* scroll = new QScrollArea(parent);
    scroll->setObjectName(QStringLiteral("AboutCreditsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* list = new QWidget(scroll);
    list->setObjectName(QStringLiteral("AboutCreditsList"));
    auto* layout = new QVBoxLayout(list);
    layout->setContentsMargins(24, 4, 24, 12);
    layout->setSpacing(6);
    const char* group = nullptr;
    for (const Credit& credit : kCredits) {
        if (!group || qstrcmp(group, credit.group) != 0) {
            group = credit.group;
            auto* heading = new QLabel(QCoreApplication::translate("AboutDialog", group).toUpper(), list);
            heading->setObjectName(QStringLiteral("AboutGroupTitle"));
            layout->addWidget(heading);
        }
        layout->addWidget(new CreditCard(credit, list));
    }
    layout->addStretch(1);
    scroll->setWidget(list);
    return scroll;
}

} // namespace

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    using namespace Hephaestus::Tools::VersionInfo;

    setObjectName(QStringLiteral("AboutDialog"));
    setWindowTitle(tr("About %1").arg(AppName()));
    setFixedSize(520, 560);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(new AboutHero(this));

    auto* header = new QVBoxLayout;
    header->setContentsMargins(24, 18, 24, 14);
    header->setSpacing(6);
    header->addWidget(CenteredLabel(AppName(), "AboutTitle", this));
    auto* version = new QLabel(tr("Version %1").arg(EngineVersion()), this);
    version->setObjectName(QStringLiteral("AboutVersion"));
    header->addWidget(version, 0, Qt::AlignHCenter);
    header->addSpacing(8);

    auto* switcher = new QFrame(this);
    switcher->setObjectName(QStringLiteral("ModeSwitch"));
    auto* switchLayout = new QHBoxLayout(switcher);
    switchLayout->setContentsMargins(2, 2, 2, 2);
    switchLayout->setSpacing(2);
    auto* pages = new QStackedWidget(this);
    pages->addWidget(BuildOverview(pages));
    pages->addWidget(BuildCredits(pages));
    auto* group = new QButtonGroup(this);
    int index = 0;
    for (const QString& label : { tr("About"), tr("Credits") }) {
        auto* button = new QToolButton(switcher);
        button->setObjectName(QStringLiteral("ModeButton"));
        button->setText(label);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setChecked(index == 0);
        group->addButton(button, index++);
        switchLayout->addWidget(button);
    }
    connect(group, &QButtonGroup::idClicked, pages, &QStackedWidget::setCurrentIndex);
    header->addWidget(switcher, 0, Qt::AlignHCenter);
    root->addLayout(header);
    root->addWidget(pages, 1);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(24, 8, 24, 16);
    footer->addStretch(1);
    auto* close = new QPushButton(tr("Close"), this);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(close);
    root->addLayout(footer);
}

} // namespace Hephaestus::QtEditor

#endif // HEPHAESTUS_EDITOR_QT_UI
