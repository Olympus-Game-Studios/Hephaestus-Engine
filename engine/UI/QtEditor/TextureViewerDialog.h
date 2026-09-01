#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QDialog>
#include <QImage>
#include <QString>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QGraphicsPixmapItem;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QPushButton;
QT_END_NAMESPACE

namespace Hephaestus {
namespace QtEditor {

/**
 * @brief Lightweight texture preview dialog with zoom/pan and channel isolation.
 */
class TextureViewerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TextureViewerDialog(const QString& absolutePath, QWidget* parent = nullptr);

    static void Open(QWidget* parent, const QString& absolutePath);

private slots:
    void onChannelToggled();
    void onCopyPath();
    void onOpenExternally();
    void onShowInExplorer();

private:
    void setupUi();
    void loadImage();
    void updatePreviewImage();
    void updateMetadataLabels();
    QImage buildDisplayImage() const;

    QString m_absolutePath;
    QString m_relativePath;
    QImage m_sourceImage;
    bool m_previewAvailable = false;
    QString m_statusNote;

    QGraphicsView* m_view = nullptr;
    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_pixmapItem = nullptr;

    QLabel* m_resolutionLabel = nullptr;
    QLabel* m_sizeLabel = nullptr;
    QLabel* m_formatLabel = nullptr;
    QLabel* m_pathLabel = nullptr;
    QLabel* m_noteLabel = nullptr;

    QCheckBox* m_channelR = nullptr;
    QCheckBox* m_channelG = nullptr;
    QCheckBox* m_channelB = nullptr;
    QCheckBox* m_channelA = nullptr;
    QCheckBox* m_visualizeAlpha = nullptr;

    QPushButton* m_copyPathBtn = nullptr;
    QPushButton* m_openExternalBtn = nullptr;
    QPushButton* m_showExplorerBtn = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
