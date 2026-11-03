#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorTheme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace Hephaestus {
namespace QtEditor {
namespace Theme {

namespace {

struct TokenEntry
{
    Token token;
    const char* name;
    QRgb rgb;
};

constexpr std::array<TokenEntry, static_cast<size_t>(Token::Count)> kTokens = {{
    { Token::SurfaceBase,           "surface-base",            0x0D1016 },
    { Token::SurfacePanel,          "surface-panel",           0x141923 },
    { Token::SurfaceRaised,         "surface-raised",          0x1C2330 },
    { Token::SurfaceControl,        "surface-control",         0x222B3A },
    { Token::SurfaceHover,          "surface-hover",           0x2A3446 },
    { Token::SurfaceSelected,       "surface-selected",        0x1F2D52 },
    { Token::SurfaceSelectedStrong, "surface-selected-strong", 0x2A4078 },
    { Token::SurfaceViewport,       "surface-viewport",        0x0A0C11 },
    { Token::BorderDefault,         "border-default",          0x313C4F },
    { Token::BorderSubtle,          "border-subtle",           0x262F3E },
    { Token::BorderFocus,           "border-focus",            0x4E7BFF },
    { Token::TextPrimary,           "text-primary",            0xE8EDF6 },
    { Token::TextSecondary,         "text-secondary",          0xB4BFD0 },
    { Token::TextMuted,             "text-muted",              0x8A96AB },
    { Token::TextDisabled,          "text-disabled",           0x5A6578 },
    { Token::TextOnAction,          "text-on-action",          0xFFFFFF },
    { Token::TextLink,              "text-link",               0x4E7BFF },
    { Token::AccentAction,          "accent-action",           0x3D63D9 },
    { Token::AccentActionHover,     "accent-action-hover",     0x3558C4 },
    { Token::AccentActionPressed,   "accent-action-pressed",   0x2C4AA8 },
    { Token::AccentSelection,       "accent-selection",        0x4E7BFF },
    { Token::AccentBrand,           "accent-brand",            0xFB712B },
    { Token::DangerFill,            "danger-fill",             0xB93F3F },
    { Token::DangerHover,           "danger-hover",            0xA63737 },
    { Token::DangerPressed,         "danger-pressed",          0x8F2F2F },
    { Token::StatusSuccess,         "status-success",          0x4FBF75 },
    { Token::StatusWarning,         "status-warning",          0xE3A23B },
    { Token::StatusError,           "status-error",            0xD85A5A },
    { Token::StatusInfo,            "status-info",             0x5A9BFF },
    { Token::StatusErrorBg,         "status-error-bg",         0x2A1A1F },
    { Token::StatusWarningBg,       "status-warning-bg",       0x2A2318 },
    { Token::StatusSuccessBg,       "status-success-bg",       0x16261E },
    { Token::StatusInfoBg,          "status-info-bg",          0x172338 },
    { Token::AxisX,                 "axis-x",                  0xE5534B },
    { Token::AxisY,                 "axis-y",                  0x5DBB63 },
    { Token::AxisZ,                 "axis-z",                  0x5A8DEE },
    { Token::CodeKeyword,           "code-keyword",            0xC792EA },
    { Token::CodeType,              "code-type",               0x5FD3E8 },
    { Token::CodeString,            "code-string",             0xC3E88D },
    { Token::CodeNumber,            "code-number",             0xF2AE6B },
    { Token::CodeComment,           "code-comment",            0x6B778C },
    { Token::CodeFunction,          "code-function",           0x82AAFF },
    { Token::CodePlain,             "code-plain",              0xE8EDF6 },
    { Token::CodeLineCurrent,       "code-line-current",       0x2A2A1C },
    { Token::CodeLineBreakpoint,    "code-line-breakpoint",    0x2A1A1F },
}};

constexpr bool TokensInEnumOrder()
{
    for (size_t i = 0; i < kTokens.size(); ++i) {
        if (static_cast<size_t>(kTokens[i].token) != i) {
            return false;
        }
    }
    return true;
}
static_assert(TokensInEnumOrder(), "kTokens must list every Token in enum order");

QColor ToneColor(IconTone tone)
{
    switch (tone) {
    case IconTone::Primary:   return Color(Token::TextPrimary);
    case IconTone::Secondary: return Color(Token::TextSecondary);
    case IconTone::Muted:     return Color(Token::TextMuted);
    case IconTone::Action:    return Color(Token::AccentSelection);
    case IconTone::OnAction:  return Color(Token::TextOnAction);
    case IconTone::Brand:     return Color(Token::AccentBrand);
    case IconTone::Success:   return Color(Token::StatusSuccess);
    case IconTone::Warning:   return Color(Token::StatusWarning);
    case IconTone::Error:     return Color(Token::StatusError);
    case IconTone::Info:      return Color(Token::StatusInfo);
    }
    return Color(Token::TextSecondary);
}

QByteArray IconSource(const QString& name)
{
    static QHash<QString, QByteArray> cache;
    auto it = cache.constFind(name);
    if (it != cache.constEnd()) {
        return it.value();
    }
    QFile file(QStringLiteral(":/editor/icons/%1.svg").arg(name));
    QByteArray data;
    if (file.open(QIODevice::ReadOnly)) {
        data = file.readAll();
    } else {
        qWarning("Editor icon '%s' is not in the icon set", qPrintable(name));
    }
    cache.insert(name, data);
    return data;
}

QPixmap RenderIcon(const QByteArray& svg, const QColor& color, const QSize& size, qreal scale)
{
    QByteArray tinted = svg;
    tinted.replace("currentColor", color.name().toLatin1());
    QSvgRenderer renderer(tinted);
    QPixmap pixmap(size * scale);
    pixmap.fill(Qt::transparent);
    if (renderer.isValid()) {
        QPainter painter(&pixmap);
        renderer.render(&painter);
    }
    pixmap.setDevicePixelRatio(scale);
    return pixmap;
}

class ToneIconEngine final : public QIconEngine
{
public:
    ToneIconEngine(QString name, QByteArray svg, QColor color)
        : m_name(std::move(name)), m_svg(std::move(svg)), m_color(color)
    {
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal scale = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        const QColor color = ColorFor(mode, state);
        const QString key = QStringLiteral("heph-icon/%1/%2/%3x%4@%5")
            .arg(m_name, color.name()).arg(size.width()).arg(size.height()).arg(scale);
        auto it = m_cache.constFind(key);
        if (it != m_cache.constEnd()) {
            return it.value();
        }
        QPixmap pixmap = RenderIcon(m_svg, color, size, scale);
        m_cache.insert(key, pixmap);
        return pixmap;
    }

    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }
    QIconEngine* clone() const override { return new ToneIconEngine(m_name, m_svg, m_color); }
    QString key() const override { return QStringLiteral("HephaestusToneIcon"); }
    QString iconName() override { return m_name; }

private:
    QColor ColorFor(QIcon::Mode mode, QIcon::State state) const
    {
        if (mode == QIcon::Disabled) {
            return Color(Token::TextDisabled);
        }
        if (mode == QIcon::Selected || state == QIcon::On) {
            return Color(Token::TextOnAction);
        }
        return m_color;
    }

    QString m_name;
    QByteArray m_svg;
    QColor m_color;
    QHash<QString, QPixmap> m_cache;
};

void LoadBundledFonts()
{
    const QString fontsDir = QCoreApplication::applicationDirPath() + QStringLiteral("/editor/fonts");
    QDirIterator it(fontsDir, { QStringLiteral("*.ttf"), QStringLiteral("*.otf") }, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFontDatabase::addApplicationFont(it.next());
    }
}

QPalette BuildPalette()
{
    QPalette p;
    auto set = [&p](QPalette::ColorRole role, Token token) { p.setColor(role, Color(token)); };
    set(QPalette::Window, Token::SurfacePanel);
    set(QPalette::WindowText, Token::TextPrimary);
    set(QPalette::Base, Token::SurfaceBase);
    set(QPalette::AlternateBase, Token::SurfaceRaised);
    set(QPalette::Text, Token::TextPrimary);
    set(QPalette::PlaceholderText, Token::TextMuted);
    set(QPalette::Button, Token::SurfaceControl);
    set(QPalette::ButtonText, Token::TextPrimary);
    set(QPalette::BrightText, Token::TextOnAction);
    set(QPalette::Highlight, Token::AccentAction);
    set(QPalette::HighlightedText, Token::TextOnAction);
    set(QPalette::Link, Token::TextLink);
    set(QPalette::LinkVisited, Token::TextLink);
    set(QPalette::ToolTipBase, Token::SurfaceRaised);
    set(QPalette::ToolTipText, Token::TextPrimary);
    set(QPalette::Light, Token::SurfaceHover);
    set(QPalette::Midlight, Token::BorderDefault);
    set(QPalette::Mid, Token::BorderSubtle);
    set(QPalette::Dark, Token::SurfaceBase);
    set(QPalette::Shadow, Token::SurfaceViewport);
    for (QPalette::ColorRole role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText }) {
        p.setColor(QPalette::Disabled, role, Color(Token::TextDisabled));
    }
    p.setColor(QPalette::Disabled, QPalette::Highlight, Color(Token::SurfaceHover));
    return p;
}

const TokenEntry* FindToken(const QString& name)
{
    for (const TokenEntry& entry : kTokens) {
        if (name == QLatin1String(entry.name)) {
            return &entry;
        }
    }
    return nullptr;
}

/** Tinted PNGs (1x/2x/3x) for `url(@icon/<name>/<token>)` in the stylesheet. */
QString StylesheetIconPath(const QString& name, const QColor& color)
{
    static QTemporaryDir dir;
    if (!dir.isValid()) {
        qWarning("Editor theme: cannot create a folder for stylesheet icons");
        return {};
    }
    const QString base = dir.path() + QStringLiteral("/%1-%2").arg(name, color.name().mid(1));
    const QString path = base + QStringLiteral(".png");
    if (!QFile::exists(path)) {
        const QByteArray svg = IconSource(name);
        RenderIcon(svg, color, QSize(16, 16), 1.0).save(path);
        RenderIcon(svg, color, QSize(16, 16), 2.0).save(base + QStringLiteral("@2x.png"));
        RenderIcon(svg, color, QSize(16, 16), 3.0).save(base + QStringLiteral("@3x.png"));
    }
    return path;
}

QString ResolveStylesheet(QString qss)
{
    static const QRegularExpression iconRef(QStringLiteral("@icon/([a-z0-9-]+)/([a-z-]+)"));
    QRegularExpressionMatchIterator icons = iconRef.globalMatch(qss);
    std::vector<std::pair<QString, QString>> iconReplacements;
    while (icons.hasNext()) {
        const QRegularExpressionMatch m = icons.next();
        const TokenEntry* token = FindToken(m.captured(2));
        if (!token) {
            qWarning("Editor theme: unknown colour token '%s'", qPrintable(m.captured(2)));
            continue;
        }
        iconReplacements.emplace_back(m.captured(0), StylesheetIconPath(m.captured(1), QColor(token->rgb)));
    }
    for (const auto& [ref, path] : iconReplacements) {
        qss.replace(ref, QStringLiteral("\"%1\"").arg(path));
    }

    std::vector<const TokenEntry*> byLength;
    for (const TokenEntry& entry : kTokens) {
        byLength.push_back(&entry);
    }
    std::sort(byLength.begin(), byLength.end(), [](const TokenEntry* a, const TokenEntry* b) {
        return qstrlen(a->name) > qstrlen(b->name);
    });
    for (const TokenEntry* entry : byLength) {
        qss.replace(QLatin1Char('@') + QLatin1String(entry->name), QColor(entry->rgb).name());
    }

    static const QRegularExpression leftover(QStringLiteral("@[a-z][a-z0-9/-]*"));
    const QRegularExpressionMatch unresolved = leftover.match(qss);
    if (unresolved.hasMatch()) {
        qWarning("Editor theme: unresolved stylesheet reference '%s'", qPrintable(unresolved.captured(0)));
    }
    return qss;
}

} // namespace

QColor Color(Token token)
{
    return QColor(kTokens[static_cast<size_t>(token)].rgb);
}

QIcon Icon(const QString& name, IconTone tone)
{
    static QHash<QString, QIcon> cache;
    const QString key = name + QLatin1Char('/') + QString::number(static_cast<int>(tone));
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) {
        return it.value();
    }
    const QByteArray svg = IconSource(name);
    QIcon icon = svg.isEmpty() ? QIcon() : QIcon(new ToneIconEngine(name, svg, ToneColor(tone)));
    cache.insert(key, icon);
    return icon;
}

void Apply(QApplication& app)
{
    LoadBundledFonts();

    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setPalette(BuildPalette());

    QFont font = app.font();
    font.setFamilies({ QStringLiteral("Inter"), font.family() });
    font.setPixelSize(13);
    app.setFont(font);

    QFile qss(QStringLiteral(":/editor/theme/editor.qss"));
    if (!qss.open(QIODevice::ReadOnly)) {
        qWarning("Editor theme: stylesheet resource is missing");
        return;
    }
    app.setStyleSheet(ResolveStylesheet(QString::fromUtf8(qss.readAll())));
}

} // namespace Theme
} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
