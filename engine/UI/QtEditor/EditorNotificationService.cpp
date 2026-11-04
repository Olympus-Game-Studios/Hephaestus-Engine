/**
 * @file EditorNotificationService.cpp
 * @brief Status-bar / toast notification sink for the Qt editor.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorNotificationService.h"
#include "EditorTheme.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <algorithm>

namespace Hephaestus {
namespace QtEditor {

EditorNotificationService::EditorNotificationService(QObject* parent)
    : QObject(parent)
{
}

void EditorNotificationService::SetStatusBar(QStatusBar* statusBar)
{
    m_statusBar = statusBar;
}

void EditorNotificationService::SetToastAnchor(QWidget* anchor)
{
    m_toastAnchor = anchor;
}

void EditorNotificationService::ShowInfo(const QString& message, int timeoutMs)
{
    showMessage(Severity::Info, message, timeoutMs, false);
}

void EditorNotificationService::ShowWarning(const QString& message, int timeoutMs)
{
    showMessage(Severity::Warning, message, timeoutMs, false);
}

void EditorNotificationService::ShowError(const QString& message, int timeoutMs)
{
    showMessage(Severity::Error, message, timeoutMs, false);
}

void EditorNotificationService::ShowProgress(const QString& message)
{
    m_progressActive = true;
    m_progressMessage = message;
    showMessage(Severity::Progress, message, 0, true);
}

void EditorNotificationService::CompleteProgress(const QString& message)
{
    m_progressActive = false;
    const QString finalMessage = message.isEmpty() ? m_progressMessage : message;
    m_progressMessage.clear();
    if (!finalMessage.isEmpty()) {
        showMessage(Severity::Info, finalMessage, 4000, false);
    } else if (m_statusBar) {
        m_statusBar->clearMessage();
    }
    hideToast();
}

void EditorNotificationService::MarkRead()
{
    if (m_unread == 0) return;
    m_unread = 0;
    emit historyChanged();
}

bool EditorNotificationService::shouldSuppress(const QString& message)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (message == m_lastMessage && (now - m_lastMessageMs) < kDedupWindowMs) {
        return true;
    }
    m_lastMessage = message;
    m_lastMessageMs = now;
    return false;
}

void EditorNotificationService::showMessage(Severity severity, const QString& message,
                                            int timeoutMs, bool sticky)
{
    if (message.isEmpty()) {
        return;
    }
    if (!sticky && shouldSuppress(message)) {
        return;
    }

    if (m_statusBar) {
        if (sticky || timeoutMs <= 0) {
            m_statusBar->showMessage(message);
        } else {
            m_statusBar->showMessage(message, timeoutMs);
        }
    }

    if (!sticky) {
        m_history.append({ severity, message, QDateTime::currentDateTime() });
        if (m_history.size() > kHistoryLimit) m_history.removeFirst();
        ++m_unread;
        emit historyChanged();
        showToast(severity, message, timeoutMs > 0 ? timeoutMs : 4000);
    }
}

void EditorNotificationService::showToast(Severity severity, const QString& message, int timeoutMs)
{
    QWidget* anchor = m_toastAnchor;
    if (!anchor) {
        return;
    }

    if (!m_toastFrame) {
        m_toastFrame = new QFrame(anchor);
        m_toastFrame->setObjectName(QStringLiteral("EditorNotificationToast"));
        m_toastFrame->setAttribute(Qt::WA_ShowWithoutActivating);
        m_toastFrame->setAttribute(Qt::WA_StyledBackground);
        m_toastFrame->setFocusPolicy(Qt::NoFocus);

        auto* layout = new QHBoxLayout(m_toastFrame);
        layout->setContentsMargins(12, 10, 8, 10);
        layout->setSpacing(10);
        m_toastIcon = new QLabel(m_toastFrame);
        layout->addWidget(m_toastIcon, 0, Qt::AlignTop);
        m_toastLabel = new QLabel(m_toastFrame);
        m_toastLabel->setWordWrap(true);
        m_toastLabel->setMinimumWidth(260);
        m_toastLabel->setMaximumWidth(420);
        layout->addWidget(m_toastLabel, 1);
        auto* close = new QToolButton(m_toastFrame);
        close->setIcon(Theme::Icon(QStringLiteral("close")));
        close->setToolTip(tr("Dismiss"));
        close->setFocusPolicy(Qt::NoFocus);
        connect(close, &QToolButton::clicked, this, &EditorNotificationService::hideToast);
        layout->addWidget(close, 0, Qt::AlignTop);
    }

    const char* icon = "info";
    Theme::IconTone tone = Theme::IconTone::Info;
    const char* level = "info";
    switch (severity) {
    case Severity::Warning: icon = "warning"; tone = Theme::IconTone::Warning; level = "warning"; break;
    case Severity::Error:   icon = "error";   tone = Theme::IconTone::Error;   level = "error";   break;
    case Severity::Progress: icon = "spinner"; tone = Theme::IconTone::Muted;  break;
    case Severity::Info:
    default: break;
    }
    m_toastIcon->setPixmap(Theme::Icon(QString::fromLatin1(icon), tone).pixmap(QSize(16, 16), m_toastFrame->devicePixelRatioF()));
    m_toastFrame->setProperty("level", QString::fromLatin1(level));
    m_toastFrame->style()->unpolish(m_toastFrame);
    m_toastFrame->style()->polish(m_toastFrame);

    if (m_toastLabel) {
        m_toastLabel->setText(message);
    }

    m_toastFrame->adjustSize();
    repositionToast();
    m_toastFrame->show();
    m_toastFrame->raise();

    QTimer::singleShot(timeoutMs, this, [this]() {
        hideToast();
    });
}

void EditorNotificationService::repositionToast()
{
    if (!m_toastFrame || !m_toastAnchor) {
        return;
    }

    const QWidget* anchor = m_toastAnchor;
    const int margin = 12;
    const int statusReserve = m_statusBar ? m_statusBar->height() + 8 : 28;
    const int x = anchor->width() - m_toastFrame->width() - margin;
    const int y = anchor->height() - statusReserve - m_toastFrame->height();
    m_toastFrame->move(x, std::max(margin, y));
}

void EditorNotificationService::hideToast()
{
    if (m_toastFrame) {
        m_toastFrame->hide();
    }
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
