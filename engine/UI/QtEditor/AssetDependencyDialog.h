/**
 * @file AssetDependencyDialog.h
 * @brief QDialog listing Dependencies / Used By for an asset path.
 */

#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QDialog>
#include <QString>
#include <functional>

class QListWidget;
class QTabWidget;
class QLabel;

namespace Hephaestus {
class AssetReferenceIndex;
}

namespace Hephaestus::QtEditor {

/**
 * @brief Shows forward dependencies and reverse referrers for an asset.
 */
class AssetDependencyDialog : public QDialog {
    Q_OBJECT

public:
    using OpenOrRevealCallback = std::function<void(const QString& portablePath)>;

    explicit AssetDependencyDialog(AssetReferenceIndex* index,
                                   const QString& assetPortablePath,
                                   QWidget* parent = nullptr);

    void SetOpenOrRevealCallback(OpenOrRevealCallback callback);

private:
    void rebuildLists();
    void onItemActivated(const QString& portablePath);

    AssetReferenceIndex* m_index = nullptr;
    QString m_assetPath;
    OpenOrRevealCallback m_openCallback;

    QLabel* m_headerLabel = nullptr;
    QTabWidget* m_tabs = nullptr;
    QListWidget* m_dependenciesList = nullptr;
    QListWidget* m_referrersList = nullptr;
};

} // namespace Hephaestus::QtEditor

#endif // HEPHAESTUS_EDITOR_QT_UI
