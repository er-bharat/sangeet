#pragma once

#include <QList>
#include <QListWidget>
#include <QPixmap>
#include <QTimer>
#include <QWidget>
#include "scanning.h"
#include <QHash>
#include <QMutex>
#include <QSet>
#include <vector>

class QGridLayout;
class QHBoxLayout;
class QVBoxLayout;
class MainWindow;
class QLabel;
class QToolButton;
class QStackedWidget;
class LoudnessProgressWidget;

class PlayerArtWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlayerArtWidget(QWidget *parent = nullptr);
    void setArtwork(const QString &path);
    void setStreamFallback(bool stream);
    void setTracks(const QList<Track> &tracks);
    void setCurrentTrack(int index);
    void showTrackListTemporarily();
    void setShuffleState(bool enabled);
    void setRepeatMode(int mode);

signals:
    void trackSelected(int index);
    void shuffleClicked();
    void repeatClicked();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void updateArtwork();
    void showOverlays();
    void hideOverlays();

    QLabel *m_art = nullptr;
    QListWidget *m_tracks = nullptr;
    QToolButton *m_shuffleButton = nullptr;
    QToolButton *m_repeatButton = nullptr;
    QTimer m_hideTimer;
    QPixmap m_pixmap;
    bool m_streamFallback = false;
};

class PlayerPage
{
public:
    explicit PlayerPage(MainWindow *window);

    void createPlayerPage();
    void openPlaylist(const Playlist &playlist, int trackIndex);
    void playPlaylistQueueTrack(int queueIndex, const Playlist &playlist);
    void openAlbum(const Album &album, int trackIndex, bool fromTracks);
    void playQueueTrack(int queueIndex);
    void nextTrack();
    void previousTrack();
    void toggleShuffle();
    void cycleRepeatMode();
    void playTrack(int index);
    void togglePlayback();
    void setPaused(bool paused);
    void seekTo(qint64 milliseconds);
    void seekBy(double milliseconds);
    void processMpvEvents();
    void updatePlaybackPosition(qint64 position);
    void updateDuration(qint64 duration);
    bool initializeMpv();
    void updateLyricsFont();
    void populateLyrics();
    void centerLyricItem(int itemIndex);
    void updateLyrics(qint64 position);
    QString formatTime(qint64 milliseconds);
    QString m_currentLoudnessPath;

private:
    void createPlayerHeader(QGridLayout *top);
    void createLyrics(QHBoxLayout *content);
    void createPlayerControls(QVBoxLayout *root);
    void connectPlayerSignals();
    void shuffleQueue();
    void restoreQueue();
    void shuffleAlbum();
    void requestLoudness(const QString &path);
    void preloadLoudness();
    void showLoudness(const QString &path);

    MainWindow *m_window = nullptr;
    LoudnessProgressWidget *m_waveform = nullptr;
    QStackedWidget *m_progressStack = nullptr;
    
    QHash<QString, std::vector<double>> m_loudnessCache;
    QSet<QString> m_loudnessPending;
    QMutex m_loudnessMutex;
};
