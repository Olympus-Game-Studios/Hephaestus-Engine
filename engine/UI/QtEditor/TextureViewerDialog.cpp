/**
 * @file TextureViewerDialog.cpp
 * @brief Lightweight Qt texture preview with channel isolation.
 *
 * Image decode is synchronous via QImage (adequate for typical editor assets).
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "TextureViewerDialog.h"

#include "../../Assets/AssetManager.h"

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace Hephaestus {
namespace QtEditor {

namespace {

class ZoomPanGraphicsView : public QGraphicsView
{
public:
    using QGraphicsView::QGraphicsView;

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        const qreal factor = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
        scale(factor, factor);
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton) {
            m_panning = true;
            m_lastPos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QGraphicsView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_panning) {
            const QPoint delta = event->pos() - m_lastPos;
            m_lastPos = event->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
            m_panning = false;
            setCursor(Qt::ArrowCursor);
            event->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }

private:
    bool m_panning = false;
    QPoint m_lastPos;
};

QPixmap makeCheckerboardPixmap(int size = 32)
{
    QPixmap pix(size * 2, size * 2);
    pix.fill(QColor(180, 180, 180));
    QPainter p(&pix);
    p.fillRect(0, 0, size, size, QColor(220, 220, 220));
    p.fillRect(size, size, size, size, QColor(220, 220, 220));
    return pix;
}

bool isHdrOrUnsupportedExtension(const QString& suffix)
{
    const QString ext = suffix.toLower();
    return ext == QStringLiteral("hdr") || ext == QStringLiteral("exr") || ext == QStringLiteral("dds");
}

} // namespace

TextureViewerDialog::TextureViewerDialog(const QString& absolutePath, QWidget* parent)
    : QDialog(parent)
    , m_absolutePath(absolutePath)
{
    setWindowTitle(tr("Texture Viewer — %1").arg(QFileInfo(absolutePath).fileName()));
    resize(900, 700);
    setAttribute(Qt::WA_DeleteOnClose);

    auto& assetMgr = AssetManager::GetInstance();
    if (assetMgr.HasValidAssetRoot()) {
        const std::string root = assetMgr.GetAssetRoot();
        QString absNative = QDir::cleanPath(absolutePath);
        QString rootNative = QDir::cleanPath(QString::fromStdString(root));
        if (absNative.startsWith(rootNative, Qt::CaseInsensitive)) {
            m_relativePath = absNative.mid(rootNative.size());
            if (m_relativePath.startsWith('/') || m_relativePath.startsWith('\\')) {
                m_relativePath = m_relativePath.mid(1);
            }
            m_relativePath.replace('\\', '/');
        }
    }
    if (m_relativePath.isEmpty()) {
        m_relativePath = QFileInfo(absolutePath).fileName();
    }

    setupUi();
    loadImage();
    updateMetadataLabels();
    updatePreviewImage();
}

void TextureViewerDialog::Open(QWidget* parent, const QString& absolutePath)
{
    auto* dialog = new TextureViewerDialog(absolutePath, parent);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void TextureViewerDialog::setupUi()
{
    auto* root = new QVBoxLayout(this);

    m_scene = new QGraphicsScene(this);
    m_view = new ZoomPanGraphicsView(m_scene, this);
    m_view->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    m_view->setDragMode(QGraphicsView::NoDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setBackgroundBrush(QBrush(makeCheckerboardPixmap()));
    m_view->setMinimumHeight(420);
    root->addWidget(m_view, 1);

    m_pixmapItem = m_scene->addPixmap(QPixmap());

    auto* metaRow = new QHBoxLayout();
    m_resolutionLabel = new QLabel(this);
    m_sizeLabel = new QLabel(this);
    m_formatLabel = new QLabel(this);
    metaRow->addWidget(m_resolutionLabel);
    metaRow->addWidget(m_sizeLabel);
    metaRow->addWidget(m_formatLabel);
    metaRow->addStretch(1);
    root->addLayout(metaRow);

    m_pathLabel = new QLabel(this);
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathLabel->setWordWrap(true);
    root->addWidget(m_pathLabel);

    m_noteLabel = new QLabel(this);
    m_noteLabel->setWordWrap(true);
    m_noteLabel->setStyleSheet(QStringLiteral("color: #b07020;"));
    root->addWidget(m_noteLabel);

    auto* channelRow = new QHBoxLayout();
    m_channelR = new QCheckBox(tr("R"), this);
    m_channelG = new QCheckBox(tr("G"), this);
    m_channelB = new QCheckBox(tr("B"), this);
    m_channelA = new QCheckBox(tr("A"), this);
    m_visualizeAlpha = new QCheckBox(tr("Visualize Alpha"), this);
    m_channelR->setChecked(true);
    m_channelG->setChecked(true);
    m_channelB->setChecked(true);
    m_channelA->setChecked(true);
    channelRow->addWidget(new QLabel(tr("Channels:"), this));
    channelRow->addWidget(m_channelR);
    channelRow->addWidget(m_channelG);
    channelRow->addWidget(m_channelB);
    channelRow->addWidget(m_channelA);
    channelRow->addWidget(m_visualizeAlpha);
    channelRow->addStretch(1);
    root->addLayout(channelRow);

    connect(m_channelR, &QCheckBox::toggled, this, &TextureViewerDialog::onChannelToggled);
    connect(m_channelG, &QCheckBox::toggled, this, &TextureViewerDialog::onChannelToggled);
    connect(m_channelB, &QCheckBox::toggled, this, &TextureViewerDialog::onChannelToggled);
    connect(m_channelA, &QCheckBox::toggled, this, &TextureViewerDialog::onChannelToggled);
    connect(m_visualizeAlpha, &QCheckBox::toggled, this, &TextureViewerDialog::onChannelToggled);

    auto* buttonRow = new QHBoxLayout();
    m_copyPathBtn = new QPushButton(tr("Copy Path"), this);
    m_openExternalBtn = new QPushButton(tr("Open Externally"), this);
    m_showExplorerBtn = new QPushButton(tr("Show in Explorer"), this);
    buttonRow->addWidget(m_copyPathBtn);
    buttonRow->addWidget(m_openExternalBtn);
    buttonRow->addWidget(m_showExplorerBtn);
    buttonRow->addStretch(1);
    auto* closeBtn = new QPushButton(tr("Close"), this);
    buttonRow->addWidget(closeBtn);
    root->addLayout(buttonRow);

    connect(m_copyPathBtn, &QPushButton::clicked, this, &TextureViewerDialog::onCopyPath);
    connect(m_openExternalBtn, &QPushButton::clicked, this, &TextureViewerDialog::onOpenExternally);
    connect(m_showExplorerBtn, &QPushButton::clicked, this, &TextureViewerDialog::onShowInExplorer);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void TextureViewerDialog::loadImage()
{
    m_previewAvailable = false;
    m_sourceImage = QImage();
    m_statusNote.clear();

    const QFileInfo info(m_absolutePath);
    const QString suffix = info.suffix();

    if (isHdrOrUnsupportedExtension(suffix)) {
        m_statusNote = tr("HDR / compressed formats are not previewed here. Metadata only — open externally to inspect.");
        return;
    }

    QImageReader reader(m_absolutePath);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        m_statusNote = tr("Could not decode image for preview (%1). Metadata is still shown.")
                           .arg(reader.errorString());
        return;
    }

    m_sourceImage = image.convertToFormat(QImage::Format_ARGB32);
    m_previewAvailable = true;
    m_statusNote = tr("Scroll to zoom, drag to pan.");
}

void TextureViewerDialog::updateMetadataLabels()
{
    const QFileInfo info(m_absolutePath);
    const qint64 bytes = info.exists() ? info.size() : 0;
    QString sizeText;
    if (bytes < 1024) {
        sizeText = tr("%1 B").arg(bytes);
    } else if (bytes < 1024 * 1024) {
        sizeText = tr("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    } else {
        sizeText = tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    }

    if (m_previewAvailable) {
        m_resolutionLabel->setText(tr("Resolution: %1 × %2")
                                       .arg(m_sourceImage.width())
                                       .arg(m_sourceImage.height()));
        m_formatLabel->setText(tr("Format: %1 (%2-bit)")
                                   .arg(info.suffix().toUpper())
                                   .arg(m_sourceImage.depth()));
    } else {
        QImageReader reader(m_absolutePath);
        const QSize size = reader.size();
        if (size.isValid()) {
            m_resolutionLabel->setText(tr("Resolution: %1 × %2").arg(size.width()).arg(size.height()));
        } else {
            m_resolutionLabel->setText(tr("Resolution: n/a"));
        }
        m_formatLabel->setText(tr("Format: %1").arg(info.suffix().isEmpty()
                                                        ? tr("unknown")
                                                        : info.suffix().toUpper()));
    }

    m_sizeLabel->setText(tr("Size: %1").arg(sizeText));
    m_pathLabel->setText(tr("Path: %1").arg(m_relativePath));
    m_noteLabel->setText(m_statusNote);
    m_noteLabel->setVisible(!m_statusNote.isEmpty());

    const bool enableChannels = m_previewAvailable;
    m_channelR->setEnabled(enableChannels);
    m_channelG->setEnabled(enableChannels);
    m_channelB->setEnabled(enableChannels);
    m_channelA->setEnabled(enableChannels);
    m_visualizeAlpha->setEnabled(enableChannels);
}

void TextureViewerDialog::updatePreviewImage()
{
    if (!m_pixmapItem) {
        return;
    }

    if (!m_previewAvailable) {
        m_pixmapItem->setPixmap(QPixmap());
        m_scene->setSceneRect(QRectF());
        return;
    }

    const QImage display = buildDisplayImage();
    m_pixmapItem->setPixmap(QPixmap::fromImage(display));
    m_scene->setSceneRect(m_pixmapItem->boundingRect());
    m_view->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
}

QImage TextureViewerDialog::buildDisplayImage() const
{
    if (!m_previewAvailable || m_sourceImage.isNull()) {
        return {};
    }

    const bool rOn = m_channelR->isChecked();
    const bool gOn = m_channelG->isChecked();
    const bool bOn = m_channelB->isChecked();
    const bool aOn = m_channelA->isChecked();
    const bool vizA = m_visualizeAlpha->isChecked();

    QImage out(m_sourceImage.size(), QImage::Format_ARGB32);
    for (int y = 0; y < m_sourceImage.height(); ++y) {
        const QRgb* src = reinterpret_cast<const QRgb*>(m_sourceImage.constScanLine(y));
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < m_sourceImage.width(); ++x) {
            const QRgb px = src[x];
            const int r = rOn ? qRed(px) : 0;
            const int g = gOn ? qGreen(px) : 0;
            const int b = bOn ? qBlue(px) : 0;
            const int a = qAlpha(px);

            if (vizA) {
                const int gray = a;
                dst[x] = qRgba(gray, gray, gray, 255);
            } else if (!aOn) {
                dst[x] = qRgba(r, g, b, 255);
            } else {
                dst[x] = qRgba(r, g, b, a);
            }
        }
    }
    return out;
}

void TextureViewerDialog::onChannelToggled()
{
    updatePreviewImage();
}

void TextureViewerDialog::onCopyPath()
{
    if (QClipboard* clipboard = QApplication::clipboard()) {
        clipboard->setText(m_relativePath.isEmpty() ? m_absolutePath : m_relativePath);
    }
}

void TextureViewerDialog::onOpenExternally()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_absolutePath));
}

void TextureViewerDialog::onShowInExplorer()
{
#ifdef _WIN32
    QProcess::startDetached(QStringLiteral("explorer"),
                            {QStringLiteral("/select,"), QDir::toNativeSeparators(m_absolutePath)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_absolutePath).absolutePath()));
#endif
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
