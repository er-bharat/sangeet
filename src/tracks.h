#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QSet>
#include <QVariant>

#include "scanning.h"
#include "albums.h"

class MainWindow;

class TrackTableModel : public QAbstractTableModel
{
public:
    explicit TrackTableModel(const QList<Album> *albums, QObject *parent = nullptr)
        : QAbstractTableModel(parent), m_albums(albums) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 7;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size() || !m_albums)
            return {};
        const TrackRef &ref = m_rows[index.row()];
        if (ref.albumIndex < 0 || ref.albumIndex >= m_albums->size())
            return {};
        const Album &album = (*m_albums)[ref.albumIndex];
        if (ref.trackIndex < 0 || ref.trackIndex >= album.tracks.size())
            return {};
        const Track &track = album.tracks[ref.trackIndex];

        if (role == Qt::CheckStateRole && index.column() == 0)
            return m_checked.contains(index.row()) ? Qt::Checked : Qt::Unchecked;
        if (role != Qt::DisplayRole)
            return {};

        switch (index.column()) {
        case 0: return {};
        case 1: return index.row() + 1;
        case 2: return track.title;
        case 3: return track.artist;
        case 4: return track.album;
        case 5: return track.fileType;
        case 6: return track.frequency > 0 ? QString::number(track.frequency) + " Hz" : QStringLiteral("—");
        default: return {};
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        static const QStringList headers = {
            QString(), QStringLiteral("Sr No."), QStringLiteral("Track name"),
            QStringLiteral("Artist"), QStringLiteral("Album"),
            QStringLiteral("Filetype"), QStringLiteral("Frequency")
        };
        return section >= 0 && section < headers.size() ? headers[section] : QVariant();
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return Qt::NoItemFlags;
        Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (index.column() == 0)
            result |= Qt::ItemIsUserCheckable;
        return result;
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override
    {
        if (!index.isValid() || index.column() != 0 || role != Qt::CheckStateRole)
            return false;
        if (value.toInt() == Qt::Checked)
            m_checked.insert(index.row());
        else
            m_checked.remove(index.row());
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }

    void setRows(const QList<TrackRef> &rows)
    {
        beginResetModel();
        m_rows = rows;
        m_checked.clear();
        endResetModel();
    }

    const QList<TrackRef> &rows() const { return m_rows; }

    QList<TrackRef> checkedRows() const
    {
        QList<int> checked = m_checked.values();
        std::sort(checked.begin(), checked.end());
        QList<TrackRef> result;
        for (const int row : checked) {
            if (row >= 0 && row < m_rows.size())
                result.append(m_rows[row]);
        }
        return result;
    }

    void clearChecks()
    {
        if (m_checked.isEmpty())
            return;
        const QSet<int> checked = m_checked;
        m_checked.clear();
        for (int row : checked) {
            if (row >= 0 && row < m_rows.size())
                emit dataChanged(index(row, 0), index(row, 0), {Qt::CheckStateRole});
        }
    }

private:
    const QList<Album> *m_albums = nullptr;
    QList<TrackRef> m_rows;
    QSet<int> m_checked;
};

class Tracks
{
public:
    explicit Tracks(MainWindow *window);

    void createTracksPage();
    void setNavigationPage(bool tracksPage, bool playlistsPage);
    void resizeTracksColumnsToContents();
    void showTracksPage(const QString &albumFilter);
    void beginPlaylistCreation();
    void cancelPlaylistCreation();
    void createPlaylistFile();

private:
    MainWindow *m_window = nullptr;
};
