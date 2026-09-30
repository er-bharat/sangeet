#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QWidget>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QTableView>
#include <QScrollArea>
#include <QGridLayout>
#include <QTimer>
#include <QLabel>
#include <QListWidget>
#include <QSlider>
#include <QCheckBox>
#include <QList>
#include <QString>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QByteArray>

#include "scanning.h"
#include <mpv/client.h>

class NavigationBar;
class PlayerArtWidget;
class TrackTableModel;
class MprisRootAdaptor;
class MprisPlayerAdaptor;
class FolderSelect;
class Albums;
class Playlists;
class Tracks;
class PlayerPage;
class Mpris;

class AppSettings
{
public:
    QString rememberedFolder() const
    {
        QSettings settings(configPath(), QSettings::IniFormat);
        if (!settings.value("rememberFolder", false).toBool())
            return {};

        const QString folder = settings.value("folder").toString();
        const QFileInfo info(folder);
        return info.exists() && info.isDir() ? info.absoluteFilePath() : QString();
    }

    bool rememberFolderEnabled() const
    {
        QSettings settings(configPath(), QSettings::IniFormat);
        return settings.value("rememberFolder", false).toBool();
    }

    void saveRememberedFolder(const QString &folder) const
    {
        QSettings settings(configPath(), QSettings::IniFormat);
        settings.setValue("rememberFolder", true);
        settings.setValue("folder", QFileInfo(folder).absoluteFilePath());
        settings.sync();
    }

    void clearRememberedFolder() const
    {
        QSettings settings(configPath(), QSettings::IniFormat);
        settings.remove("folder");
        settings.setValue("rememberFolder", false);
        settings.sync();
    }
    
    int progressBarMode() const
    {
        QSettings settings(configPath(), QSettings::IniFormat);
        return settings.value("progressBarMode", 0).toInt();
    }
    
    void saveProgressBarMode(int mode) const
    {
        QSettings settings(configPath(), QSettings::IniFormat);
        settings.setValue("progressBarMode", mode);
        settings.sync();
    }
    
    QString alsaCardName() const
    {
        QSettings settings(configPath(), QSettings::IniFormat);
        return settings.value("alsa").toString();
    }
    
    void saveAlsaCardName(const QString &name) const
    {
        QSettings settings(configPath(), QSettings::IniFormat);
        settings.setValue("alsa", name);
        settings.sync();
    }
    
    bool alsaEnabled() const
    {
        QSettings settings(configPath(), QSettings::IniFormat);
        return settings.value("alsaEnabled", false).toBool();
    }
    
    void saveAlsaEnabled(bool enabled) const
    {
        QSettings settings(configPath(), QSettings::IniFormat);
        settings.setValue("alsaEnabled", enabled);
        settings.sync();
    }
    
    bool albumArtBlurEnabled() const
    {
        QSettings settings(configPath(), QSettings::IniFormat);
        return settings.value("albumArtBlur", true).toBool();
    }
    
    void saveAlbumArtBlurEnabled(bool enabled) const
    {
        QSettings settings(configPath(), QSettings::IniFormat);
        settings.setValue("albumArtBlur", enabled);
        settings.sync();
    }
    
    QByteArray eqStateJson() const
    {
        QSettings s(configPath(), QSettings::IniFormat);
        return QByteArray::fromBase64(s.value("equalizer/state").toString().toLatin1());
    }
    void saveEqStateJson(const QByteArray &json) const
    {
        QSettings s(configPath(), QSettings::IniFormat);
        s.setValue("equalizer/state", QString::fromLatin1(json.toBase64()));
        s.sync();
    }
    QByteArray eqPresetsJson() const
    {
        QSettings s(configPath(), QSettings::IniFormat);
        return QByteArray::fromBase64(s.value("equalizer/userPresets").toString().toLatin1());
    }
    void saveEqPresetsJson(const QByteArray &json) const
    {
        QSettings s(configPath(), QSettings::IniFormat);
        s.setValue("equalizer/userPresets", QString::fromLatin1(json.toBase64()));
        s.sync();
    }

private:
    QString configPath() const
    {
        const QString directory =
            QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        QDir().mkpath(directory);
        const QString path = QDir(directory).filePath("Sangeet.conf");
        const QString oldPath = QDir(directory).filePath("QtMusicPlayer.conf");
        if (!QFileInfo::exists(path) && QFileInfo::exists(oldPath))
            QFile::copy(oldPath, path);
        return path;
    }
};


struct LyricLine {
    qint64 position = 0;
    QString text;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
    friend class MprisRootAdaptor;
    friend class MprisPlayerAdaptor;
    friend class Scanning;
    friend class FolderSelect;
    friend class Albums;
    friend class Playlists;
    friend class Tracks;
    friend class PlayerPage;
    friend class Mpris;
    friend int main(int, char **);

public:
    MainWindow();
    void reloadLibrary();

signals:
    void rememberFolderRequested(const QString &directory);
    void albumsGridRebuildRequested();
    void openAlbumRequested(const Album &album, int trackIndex = 0, bool fromTracks = false);
    void openPlaylistRequested(const Playlist &playlist, int trackIndex = 0);
    void lyricsFontUpdateRequested();
    void tracksNavigationRequested(bool tracksPage, bool playlistsPage = false);
    void showPlaylistsRequested();
    void showTracksRequested(const QString &albumFilter = QString());
    void playlistsRebuildRequested();

private:
    Scanning *m_scanning = nullptr;
    FolderSelect *m_folderSelect = nullptr;
    Albums *m_albumsController = nullptr;
    Playlists *m_playlistsController = nullptr;
    Tracks *m_tracksController = nullptr;
    PlayerPage *m_playerPageController = nullptr;
    Mpris *m_mprisController = nullptr;

public:
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

    QStackedWidget *m_stack = nullptr;
    QWidget *m_startPage = nullptr;
    QWidget *m_albumsPage = nullptr;
    QWidget *m_tracksPage = nullptr;
    QWidget *m_playlistsPage = nullptr;
    QWidget *m_playerPage = nullptr;

    QLineEdit *m_search = nullptr;
    QLineEdit *m_tracksSearch = nullptr;
    QLineEdit *m_playlistsSearch = nullptr;
    QToolButton *m_searchButton = nullptr;
    QToolButton *m_tracksSearchButton = nullptr;
    QToolButton *m_playlistsSearchButton = nullptr;
    QPushButton *m_createPlaylistButton = nullptr;
    QWidget *m_playlistBar = nullptr;
    QPushButton *m_playlistCancelButton = nullptr;
    QPushButton *m_playlistCreateButton = nullptr;
    QTableView *m_tracksTable = nullptr;
    TrackTableModel *m_tracksModel = nullptr;
    QScrollArea *m_playlistsScroll = nullptr;
    QWidget *m_playlistsContainer = nullptr;
    QGridLayout *m_playlistsGrid = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_albumsContainer = nullptr;
    QGridLayout *m_grid = nullptr;
    QTimer *m_gridRebuildTimer = nullptr;
    QTimer *m_artworkTimer = nullptr;

    PlayerArtWidget *m_artWidget = nullptr;
    QToolButton *m_albumName = nullptr;
    QLabel *m_trackTitle = nullptr;
    QLabel *m_trackArtist = nullptr;
    QListWidget *m_lyrics = nullptr;
    QPushButton *m_backToAlbums = nullptr;
    QToolButton *m_playButton = nullptr;
    NavigationBar *m_albumsNavigation = nullptr;
    NavigationBar *m_playlistsNavigation = nullptr;
    NavigationBar *m_tracksNavigation = nullptr;
    QSlider *m_positionSlider = nullptr;
    QLabel *m_currentTime = nullptr;
    QLabel *m_duration = nullptr;

    mpv_handle *m_mpv = nullptr;
    QTimer *m_mpvTimer = nullptr;
    bool m_isPlaying = false;
    bool m_trackEnded = false;

    QList<LyricLine> m_lyricsData;
    int m_activeLyric = -1;
    AppSettings m_settings;
    QList<Album> m_albums;
    QList<Album> m_filteredAlbums;
    QList<Playlist> m_playlists;
    QList<TrackRef> m_trackRows;
    QList<TrackRef> m_playbackQueue;
    int m_playbackQueueIndex = -1;
    bool m_playbackFromTracks = false;
    bool m_playbackFromPlaylist = false;
    bool m_shuffleEnabled = false;
    int m_repeatMode = 0;
    QList<int> m_albumPlaybackOrder;
    QList<TrackRef> m_sequentialPlaybackQueue;
    bool m_creatingPlaylist = false;
    QString m_playlistCreationName;
    QString m_musicBaseDirectory;
    QCheckBox *m_rememberFolder = nullptr;
    Playlist m_currentPlaylist;
    QString m_tracksAlbumFilter;
    int m_tracksSortColumn = 1;
    bool m_tracksSortAscending = true;
    bool m_tracksColumnsInitialized = false;
    Album m_currentAlbum;
    int m_currentTrack = -1;
};

// Controller implementations own their complete behavior.


