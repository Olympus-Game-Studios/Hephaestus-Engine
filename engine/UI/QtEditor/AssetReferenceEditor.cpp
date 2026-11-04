#if HEPHAESTUS_EDITOR_QT_UI

#include "AssetReferenceEditor.h"
#include "EditorTheme.h"
#include "AssetPickerDialog.h"
#include "InspectorMime.h"

#include "../../Assets/AssetManager.h"
#include "../../Assets/AssetPathPolicy.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QMimeData>
#include <QStyle>
#include <QVBoxLayout>
#include <filesystem>

namespace Hephaestus {
namespace QtEditor {

AssetReferenceEditor::AssetReferenceEditor(const QString& label, AssetType type, QWidget* parent)
    : QWidget(parent)
    , m_type(type)
{
    setAcceptDrops(true);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(2);
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    if (!label.isEmpty()) {
        m_label = new QLabel(label, this);
        row->addWidget(m_label);
    }
    m_name = new QLineEdit(this);
    m_name->setReadOnly(true);
    m_name->setPlaceholderText(tr("(None)"));
    m_browseBtn = new QPushButton(this);
    m_browseBtn->setIcon(Theme::Icon(QStringLiteral("search")));
    m_browseBtn->setProperty("iconOnly", true);
    m_browseBtn->setToolTip(tr("Browse assets"));
    m_clearBtn = new QPushButton(this);
    m_clearBtn->setIcon(Theme::Icon(QStringLiteral("close")));
    m_clearBtn->setProperty("iconOnly", true);
    m_clearBtn->setToolTip(tr("Clear"));
    m_openBtn = new QPushButton(tr("Open"), this);
    row->addWidget(m_name, 1);
    row->addWidget(m_browseBtn);
    row->addWidget(m_clearBtn);
    row->addWidget(m_openBtn);
    outer->addLayout(row);
    m_warning = new QLabel(this);
    m_warning->setProperty("role", "error");
    m_warning->hide();
    outer->addWidget(m_warning);

    connect(m_browseBtn, &QPushButton::clicked, this, &AssetReferenceEditor::browse);
    connect(m_clearBtn, &QPushButton::clicked, this, &AssetReferenceEditor::clearPath);
    connect(m_openBtn, &QPushButton::clicked, this, [this]() {
        if (!m_path.isEmpty() && !m_mixed) emit openRequested(m_path);
    });

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        if (m_missing && !m_path.isEmpty()) {
            menu.addAction(tr("Locate Replacement"), this, [this]() { browse(); });
            menu.addAction(tr("Clear"), this, [this]() { clearPath(); });
            menu.addAction(tr("Copy Missing Path"), this, [this]() { emit copyPathRequested(m_path); });
            menu.addAction(tr("Find References"), this, [this]() { emit findReferencesRequested(m_path); });
        } else {
            menu.addAction(tr("Browse…"), this, [this]() { browse(); });
            menu.addAction(tr("Open Asset"), this, [this]() {
                if (!m_path.isEmpty()) emit openRequested(m_path);
            });
            menu.addAction(tr("Reveal in Content Browser"), this, [this]() {
                if (!m_path.isEmpty()) emit revealRequested(m_path);
            });
            menu.addAction(tr("Copy Asset Path"), this, [this]() { emit copyPathRequested(m_path); });
            menu.addAction(tr("Clear"), this, [this]() { clearPath(); });
        }
        menu.exec(mapToGlobal(pos));
    });
}

void AssetReferenceEditor::setPath(const QString& portablePath, bool mixed, bool missing)
{
    m_path = portablePath;
    m_mixed = mixed;
    m_missing = missing && !mixed && !portablePath.isEmpty();
    refreshDisplay();
}

void AssetReferenceEditor::setInvalid(bool invalid)
{
    if (m_name->property("invalid").toBool() == invalid) return;
    m_name->setProperty("invalid", invalid);
    m_name->style()->unpolish(m_name);
    m_name->style()->polish(m_name);
}

void AssetReferenceEditor::refreshDisplay()
{
    if (m_mixed) {
        m_name->setText(QString());
        m_name->setPlaceholderText(QString::fromUtf8("\xE2\x80\x94 Mixed \xE2\x80\x94"));
        setInvalid(false);
        m_warning->hide();
        m_openBtn->setEnabled(false);
        m_clearBtn->setEnabled(true);
        return;
    }
    m_name->setPlaceholderText(tr("(None)"));
    if (m_path.isEmpty()) {
        m_name->clear();
        m_name->setToolTip(QString());
        setInvalid(false);
        m_warning->hide();
        m_openBtn->setEnabled(false);
        return;
    }
    m_name->setText(QFileInfo(m_path).fileName());
    m_name->setToolTip(m_path);
    m_openBtn->setEnabled(true);
    if (m_missing) {
        m_warning->setText(tr("Missing: %1").arg(m_path));
        m_warning->show();
        setInvalid(true);
    } else {
        m_warning->hide();
        setInvalid(false);
    }
}

void AssetReferenceEditor::browse()
{
    auto chosen = AssetPickerDialog::Choose(m_type, this, m_path);
    if (!chosen.has_value()) return;
    m_mixed = false;
    m_path = chosen.value();
    m_missing = false;
    refreshDisplay();
    emit pathEdited(m_path);
}

void AssetReferenceEditor::clearPath()
{
    if (!m_allowEmpty && m_path.isEmpty() && !m_mixed) return;
    m_mixed = false;
    m_path.clear();
    m_missing = false;
    refreshDisplay();
    emit pathEdited(QString());
}

bool AssetReferenceEditor::acceptDrop(const QMimeData* mime, QString* outPath) const
{
    InspectorAssetDrop drop;
    if (!ExtractInspectorAssetDrop(mime, drop)) {
        return false;
    }
    if (!AssetTypeCompatible(m_type, drop.type) && drop.type != AssetType::Unknown) {
        return false;
    }
    if (drop.type == AssetType::Unknown) {
        const AssetType inferred = AssetTypeFromExtension(
            std::filesystem::path(drop.portablePath.toStdString()).extension().string());
        if (!AssetTypeCompatible(m_type, inferred)) {
            return false;
        }
    }
    if (outPath) *outPath = drop.portablePath;
    return true;
}

void AssetReferenceEditor::dragEnterEvent(QDragEnterEvent* event)
{
    if (acceptDrop(event->mimeData(), nullptr)) {
        event->acceptProposedAction();
    }
}

void AssetReferenceEditor::dragMoveEvent(QDragMoveEvent* event)
{
    if (acceptDrop(event->mimeData(), nullptr)) {
        event->acceptProposedAction();
    }
}

void AssetReferenceEditor::dropEvent(QDropEvent* event)
{
    QString path;
    if (!acceptDrop(event->mimeData(), &path)) {
        return;
    }
    m_mixed = false;
    m_path = path;
    m_missing = false;
    refreshDisplay();
    emit pathEdited(m_path);
    event->acceptProposedAction();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
