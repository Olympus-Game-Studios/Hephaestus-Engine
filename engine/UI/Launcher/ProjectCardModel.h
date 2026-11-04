#pragma once

#include "LauncherProject.h"

#include <QAbstractListModel>
#include <QVector>

namespace Hephaestus::Launcher {

/** "2 hours ago", "Yesterday", "3 days ago", "Last week", then the date, for a project's last change. */
QString RelativeTimeText(const QDateTime& when, const QDateTime& now);

enum class ProjectSortMode {
    Name,
    LastModified
};

class ProjectCardModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        DescriptionRole,
        EngineVersionRole,
        TagsRole,
        FavoriteRole,
        BrokenRole,
        BrokenReasonRole,
        ThumbnailRole,
        ProjectPathRole,
        CreatedRole,
        LastModifiedRole,
        EditorModeRole
    };

    explicit ProjectCardModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void SetProjects(const QVector<LauncherProject>& projects);
    void SetSearchText(const QString& text);
    void SetSortMode(ProjectSortMode mode);

    LauncherProject ProjectAt(const QModelIndex& index) const;
    LauncherProject ProjectAtRow(int row) const;
    int RowForProjectPath(const QString& projectDir) const;

private:
    void RebuildView();

private:
    QVector<LauncherProject> m_allProjects;
    QVector<int> m_visibleIndices;
    QString m_searchText;
    ProjectSortMode m_sortMode = ProjectSortMode::LastModified;
};

} // namespace Hephaestus::Launcher
