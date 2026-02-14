#include "ProjectCardModel.h"

#include <QCollator>
#include <QDir>
#include <algorithm>

namespace Hephaestus::Launcher {

ProjectCardModel::ProjectCardModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ProjectCardModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_visibleIndices.size());
}

QVariant ProjectCardModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_visibleIndices.size())) {
        return {};
    }

    const LauncherProject& project = m_allProjects[m_visibleIndices[index.row()]];

    switch (role) {
        case Qt::DisplayRole:
        case NameRole:
            return project.name;
        case DescriptionRole:
            return project.description;
        case EngineVersionRole:
            return project.engineVersion;
        case TagsRole:
            return project.tags;
        case FavoriteRole:
            return project.favorite;
        case BrokenRole:
            return project.broken;
        case BrokenReasonRole:
            return project.brokenReason;
        case ThumbnailRole:
            return project.screenshotAbsolutePath;
        case ProjectPathRole:
            return project.projectDir;
        case CreatedRole:
            return project.created;
        case LastModifiedRole:
            return project.lastModified;
        default:
            return {};
    }
}

QHash<int, QByteArray> ProjectCardModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {DescriptionRole, "description"},
        {EngineVersionRole, "engineVersion"},
        {TagsRole, "tags"},
        {FavoriteRole, "favorite"},
        {BrokenRole, "broken"},
        {BrokenReasonRole, "brokenReason"},
        {ThumbnailRole, "thumbnailPath"},
        {ProjectPathRole, "projectPath"},
        {CreatedRole, "created"},
        {LastModifiedRole, "lastModified"}
    };
}

void ProjectCardModel::SetProjects(const QVector<LauncherProject>& projects)
{
    beginResetModel();
    m_allProjects = projects;
    endResetModel();
    RebuildView();
}

void ProjectCardModel::SetSearchText(const QString& text)
{
    m_searchText = text.trimmed();
    RebuildView();
}

void ProjectCardModel::SetSortMode(ProjectSortMode mode)
{
    m_sortMode = mode;
    RebuildView();
}

LauncherProject ProjectCardModel::ProjectAt(const QModelIndex& index) const
{
    return ProjectAtRow(index.row());
}

LauncherProject ProjectCardModel::ProjectAtRow(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_visibleIndices.size())) {
        return {};
    }

    return m_allProjects[m_visibleIndices[row]];
}

int ProjectCardModel::RowForProjectPath(const QString& projectDir) const
{
    const QString normalized = QDir::cleanPath(projectDir);
    for (int row = 0; row < static_cast<int>(m_visibleIndices.size()); ++row) {
        const LauncherProject& project = m_allProjects[m_visibleIndices[row]];
        if (QDir::cleanPath(project.projectDir) == normalized) {
            return row;
        }
    }
    return -1;
}

bool ProjectCardModel::SetFavorite(const QModelIndex& index, bool favorite)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_visibleIndices.size())) {
        return false;
    }

    const int backingIndex = m_visibleIndices[index.row()];
    if (backingIndex < 0 || backingIndex >= static_cast<int>(m_allProjects.size())) {
        return false;
    }

    m_allProjects[backingIndex].favorite = favorite;
    emit dataChanged(index, index, {FavoriteRole});
    return true;
}

void ProjectCardModel::RebuildView()
{
    beginResetModel();

    m_visibleIndices.clear();
    m_visibleIndices.reserve(m_allProjects.size());

    for (int i = 0; i < static_cast<int>(m_allProjects.size()); ++i) {
        const LauncherProject& project = m_allProjects[i];

        if (!m_searchText.isEmpty()) {
            const bool matches =
                project.name.contains(m_searchText, Qt::CaseInsensitive) ||
                project.description.contains(m_searchText, Qt::CaseInsensitive) ||
                project.projectDir.contains(m_searchText, Qt::CaseInsensitive);
            if (!matches) {
                continue;
            }
        }

        m_visibleIndices.push_back(i);
    }

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);

    std::sort(m_visibleIndices.begin(), m_visibleIndices.end(), [&](int lhsIndex, int rhsIndex) {
        const LauncherProject& lhs = m_allProjects[lhsIndex];
        const LauncherProject& rhs = m_allProjects[rhsIndex];

        if (m_sortMode == ProjectSortMode::Name) {
            return collator.compare(lhs.name, rhs.name) < 0;
        }

        const qint64 lhsEpoch = lhs.lastModified.toSecsSinceEpoch();
        const qint64 rhsEpoch = rhs.lastModified.toSecsSinceEpoch();
        if (lhsEpoch == rhsEpoch) {
            return collator.compare(lhs.name, rhs.name) < 0;
        }
        return lhsEpoch > rhsEpoch;
    });

    endResetModel();
}

} // namespace Hephaestus::Launcher
