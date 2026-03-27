#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QString>

class QWidget;

namespace Hephaestus {
namespace QtEditor {

bool OpenHUIVisualEditorDialog(QWidget* parent, const QString& absolutePath);

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
