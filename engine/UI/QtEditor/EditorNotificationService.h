#pragma once

/**
 * @file EditorNotificationService.h
 * @brief Lightweight editor notifications via status bar and optional toast.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QObject>
#include <QPointer>
#include <QString>

class QFrame;
class QLabel;
class QStatusBar;
class QWidget;

namespace Hephaestus {
namespace QtEditor {

/**
 * Non-modal editor feedback: status bar sink plus a brief toast near the
 * status bar. Identical messages within a short window are suppressed.
 */
class EditorNotificationService : public QObject
{
    Q_OBJECT

public:
    explicit EditorNotificationService(QObject* parent = nullptr);

    void SetStatusBar(QStatusBar* statusBar);
    /** Widget used to position the toast (typically the main window). */
    void SetToastAnchor(QWidget* anchor);

    void ShowInfo(const QString& message, int timeoutMs = 4000);
    void ShowWarning(const QString& message, int timeoutMs = 5000);
    void ShowError(const QString& message, int timeoutMs = 8000);
    void ShowProgress(const QString& message);
    void CompleteProgress(const QString& message = QString());

private:
    enum class Severity { Info, Warning, Error, Progress };

    void showMessage(Severity severity, const QString& message, int timeoutMs, bool sticky);
    bool shouldSuppress(const QString& message);
    void showToast(Severity severity, const QString& message, int timeoutMs);
    void hideToast();
    void repositionToast();

    QPointer<QStatusBar> m_statusBar;
    QPointer<QWidget> m_toastAnchor;
    QPointer<QFrame> m_toastFrame;
    QPointer<QLabel> m_toastLabel;

    QString m_lastMessage;
    qint64 m_lastMessageMs = 0;
    static constexpr int kDedupWindowMs = 2500;

    QString m_progressMessage;
    bool m_progressActive = false;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
