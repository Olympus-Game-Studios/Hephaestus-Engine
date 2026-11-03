#pragma once

/**
 * @file EditorTheme.h
 * @brief Editor 2.0 design tokens and application-wide theme.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;

namespace Hephaestus {
namespace QtEditor {
namespace Theme {

enum class Token
{
    SurfaceBase,
    SurfacePanel,
    SurfaceRaised,
    SurfaceControl,
    SurfaceHover,
    SurfaceSelected,
    SurfaceSelectedStrong,
    SurfaceViewport,
    BorderDefault,
    BorderSubtle,
    BorderFocus,
    TextPrimary,
    TextSecondary,
    TextMuted,
    TextDisabled,
    TextOnAction,
    TextLink,
    AccentAction,
    AccentActionHover,
    AccentActionPressed,
    AccentSelection,
    AccentBrand,
    DangerFill,
    DangerHover,
    DangerPressed,
    StatusSuccess,
    StatusWarning,
    StatusError,
    StatusInfo,
    StatusErrorBg,
    StatusWarningBg,
    StatusSuccessBg,
    StatusInfoBg,
    AxisX,
    AxisY,
    AxisZ,
    CodeKeyword,
    CodeType,
    CodeString,
    CodeNumber,
    CodeComment,
    CodeFunction,
    CodePlain,
    CodeLineCurrent,
    CodeLineBreakpoint,
    Count
};

QColor Color(Token token);

/** Icon tones from the design's IconTone collection. */
enum class IconTone
{
    Primary,
    Secondary,
    Muted,
    Action,
    OnAction,
    Brand,
    Success,
    Warning,
    Error,
    Info
};

/**
 * Icon from the editor icon set (Branding/design-system-export/icons), drawn
 * in @p tone. Disabled renders in text/disabled; checked or selected renders
 * in text/on-action.
 */
QIcon Icon(const QString& name, IconTone tone = IconTone::Secondary);

/** The Hephaestus logo from Branding/logo/svgs/HephaestusEngineLogo-Main.svg. */
QIcon BrandLogo();

/** Installs fonts, the Fusion base style, the palette and the stylesheet. */
void Apply(QApplication& app);

} // namespace Theme
} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
