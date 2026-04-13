#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

namespace Hephaestus::QtEditor {

class AboutDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

} // namespace Hephaestus::QtEditor

#endif // HEPHAESTUS_EDITOR_QT_UI
