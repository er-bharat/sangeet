#include <QApplication>
#include <QDialog>
#include <QMainWindow>
#include <QStackedWidget>
#include <QWidget>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QCheckBox>
#include <QScrollArea>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QPixmap>
#include <QImage>
#include <QImageReader>
#include <QMessageBox>
#include <QStandardPaths>
#include <QSettings>
#include <QSignalBlocker>
#include <QByteArray>
#include <QSet>
#include <QMap>
#include <QResizeEvent>
#include <QTimer>
#include <QFont>
#include <QSlider>
#include <QListWidget>
#include <QListWidgetItem>
#include <QListView>
#include <QAbstractListModel>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QStyle>
#include <QTableView>
#include <QAbstractTableModel>
#include <QHeaderView>
#include <QScrollBar>
#include <QAbstractItemView>
#include <QRegularExpression>
#include <QFile>
#include <QUrl>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTextStream>
#include <QEnterEvent>
#include <QVariantMap>
#include <QIcon>
#include <QRandomGenerator>
#include <QtDBus/QDBusAbstractAdaptor>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusObjectPath>
#include <algorithm>
#include <clocale>
#include <QDrag>
#include <QPainter>
#include <QStyleOptionViewItem>
#include "scanning.h"
#include "mainwindow.h"
#include "folder_select.h"
#include "albums.h"
#include "playlist.h"
#include "tracks.h"
#include "player.h"

#include <mpv/client.h>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mp4item.h>

class MainWindow;

class MprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    explicit MprisRootAdaptor(MainWindow *parent);

    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("Sangeet"); }
    QString desktopEntry() const { return QStringLiteral("Sangeet"); }
    QStringList supportedUriSchemes() const { return {QStringLiteral("file")}; }
    QStringList supportedMimeTypes() const { return {}; }

public slots:
    void Raise();
    void Quit();
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qint64 Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    explicit MprisPlayerAdaptor(MainWindow *parent);

    QString playbackStatus() const;
    double rate() const { return 1.0; }
    void setRate(double value) { Q_UNUSED(value); }
    QVariantMap metadata() const;
    qint64 position() const;
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const { return true; }
    bool canControl() const { return true; }

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qint64 offset);
    void SetPosition(const QDBusObjectPath &trackId, qint64 position);
};

// =========================================================
// Main application controller
//
// MainWindow owns application state and coordinates the page widgets.
// UI-only reusable components live above this class; scanning/settings are
// kept in service classes so changes do not spread through the controller.
// =========================================================


class MainWindow;

// ============================================================
// SCANNING
// ============================================================

class Scanning;
class FolderSelect;
class Albums;
class Playlists;
class Tracks;
class PlayerPage;
class Mpris;

class Mpris
{
public:
    explicit Mpris(MainWindow *window) : m_window(window) {}

    void initializeMpris()
    {
            if (!QDBusConnection::sessionBus().registerService(
                    QStringLiteral("org.mpris.MediaPlayer2.Sangeet"))) {
                return;
            }
    
            new MprisRootAdaptor(m_window);
            new MprisPlayerAdaptor(m_window);
    
            QDBusConnection::sessionBus().registerObject(
                QStringLiteral("/org/mpris/MediaPlayer2"),
                m_window,
                QDBusConnection::ExportAdaptors
            );
        
    }

private:
    MainWindow *m_window;
};

MprisRootAdaptor::MprisRootAdaptor(MainWindow *parent)
    : QDBusAbstractAdaptor(parent)
{
}

void MprisRootAdaptor::Raise()
{
    if (!parent())
        return;

    auto *window = static_cast<MainWindow *>(parent());
    window->showNormal();
    window->raise();
    window->activateWindow();
}

void MprisRootAdaptor::Quit()
{
    qApp->quit();
}

MprisPlayerAdaptor::MprisPlayerAdaptor(MainWindow *parent)
    : QDBusAbstractAdaptor(parent)
{
}

QString MprisPlayerAdaptor::playbackStatus() const
{
    const auto *window = static_cast<const MainWindow *>(parent());

    if (!window || window->m_currentTrack < 0)
        return QStringLiteral("Stopped");

    return window->m_isPlaying
        ? QStringLiteral("Playing")
        : QStringLiteral("Paused");
}

QVariantMap MprisPlayerAdaptor::metadata() const
{
    QVariantMap result;
    const auto *window = static_cast<const MainWindow *>(parent());

    if (!window || window->m_currentTrack < 0 ||
        window->m_currentTrack >= window->m_currentAlbum.tracks.size()) {
        result[QStringLiteral("mpris:trackid")] =
            QVariant::fromValue(QDBusObjectPath(
                QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack")));
        return result;
    }

    const Track &track = window->m_currentAlbum.tracks[window->m_currentTrack];
    const QString id = QStringLiteral("/org/mpris/MediaPlayer2/Track/") +
                       QString::number(window->m_currentTrack + 1);

    result[QStringLiteral("mpris:trackid")] =
        QVariant::fromValue(QDBusObjectPath(id));
    result[QStringLiteral("xesam:title")] = track.title;
    result[QStringLiteral("xesam:artist")] = QStringList{track.artist};
    result[QStringLiteral("xesam:album")] = track.album;
    result[QStringLiteral("xesam:albumArtist")] =
        QStringList{window->m_currentAlbum.artist};

    return result;
}

qint64 MprisPlayerAdaptor::position() const
{
    const auto *window = static_cast<const MainWindow *>(parent());
    return window ? window->m_positionSlider->value() * 1000LL : 0;
}

bool MprisPlayerAdaptor::canGoNext() const
{
    const auto *window = static_cast<const MainWindow *>(parent());
    return window && ((window->m_playbackFromPlaylist &&
                       window->m_playbackQueueIndex + 1 < window->m_playbackQueue.size()) ||
                      (window->m_playbackFromTracks &&
                       window->m_playbackQueueIndex + 1 < window->m_playbackQueue.size()) ||
                      (!window->m_playbackFromTracks &&
                       window->m_currentTrack + 1 < window->m_currentAlbum.tracks.size()));
}

bool MprisPlayerAdaptor::canGoPrevious() const
{
    const auto *window = static_cast<const MainWindow *>(parent());
    return window && ((window->m_playbackFromPlaylist && window->m_playbackQueueIndex > 0) ||
                      (window->m_playbackFromTracks && window->m_playbackQueueIndex > 0) ||
                      (!window->m_playbackFromTracks && window->m_currentTrack > 0));
}

bool MprisPlayerAdaptor::canPlay() const
{
    const auto *window = static_cast<const MainWindow *>(parent());
    return window && window->m_currentTrack >= 0;
}

bool MprisPlayerAdaptor::canPause() const
{
    return canPlay();
}

void MprisPlayerAdaptor::Next()
{
    auto *window = static_cast<MainWindow *>(parent());
    if (window)
        window->m_playerPageController->nextTrack();
}

void MprisPlayerAdaptor::Previous()
{
    auto *window = static_cast<MainWindow *>(parent());
    if (window)
        window->m_playerPageController->previousTrack();
}

void MprisPlayerAdaptor::Pause()
{
    auto *window = static_cast<MainWindow *>(parent());
    if (window)
        window->m_playerPageController->setPaused(true);
}

void MprisPlayerAdaptor::PlayPause()
{
    auto *window = static_cast<MainWindow *>(parent());
    if (window)
        window->m_playerPageController->togglePlayback();
}

void MprisPlayerAdaptor::Stop()
{
    auto *window = static_cast<MainWindow *>(parent());
    if (window)
        window->m_playerPageController->setPaused(true);
}

void MprisPlayerAdaptor::Play()
{
    auto *window = static_cast<MainWindow *>(parent());
    if (window)
        window->m_playerPageController->setPaused(false);
}

void MprisPlayerAdaptor::Seek(qint64 offset)
{
    auto *window = static_cast<MainWindow *>(parent());
    if (window)
        window->m_playerPageController->seekBy(static_cast<double>(offset) / 1000.0);
}

void MprisPlayerAdaptor::SetPosition(
    const QDBusObjectPath &trackId,
    qint64 position)
{
    Q_UNUSED(trackId);

    auto *window = static_cast<MainWindow *>(parent());
    if (window)
        window->m_playerPageController->seekTo(static_cast<int>(qMax<qint64>(0, position / 1000)));
}

#include "main.moc"

int main(int argc, char *argv[])
{
    // Qt/libmpv must start with a C numeric locale.
    setlocale(LC_NUMERIC, "C");

    QApplication app(argc, argv);
    app.setApplicationName("Sangeet");
    app.setApplicationDisplayName("Sangeet");
    app.setDesktopFileName("Sangeet");

    QIcon appIcon = sangeetIcon();
    if (appIcon.isNull())
        appIcon = QIcon::fromTheme("audio-x-generic", QIcon::fromTheme("multimedia-player"));
    app.setWindowIcon(appIcon);

    MainWindow window;

    if (argc > 1) {
        const QString path =
            QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
        QTimer::singleShot(0, &window, [&window, path]() {
            window.m_folderSelect->openPath(path);
        });
    } else {
        window.m_folderSelect->restoreRememberedFolder();
    }

    window.show();

    return app.exec();
}

void MainWindow::reloadLibrary()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_scanning->loadMusic(m_musicBaseDirectory);
    m_albums = m_scanning->albums();
    m_playlists = m_scanning->playlists();
    m_filteredAlbums = m_albums;
    QApplication::restoreOverrideCursor();
}

MainWindow::MainWindow()
{
    m_scanning = new Scanning;
    m_folderSelect = new FolderSelect(this);
    m_albumsController = new Albums(this);
    m_playlistsController = new Playlists(this);
    m_tracksController = new Tracks(this);
    m_playerPageController = new PlayerPage(this);
    m_mprisController = new Mpris(this);

    QObject::connect(this, &MainWindow::rememberFolderRequested, this, [this](const QString &directory) {
        m_folderSelect->saveRememberedFolder(directory);
    });
    QObject::connect(this, &MainWindow::albumsGridRebuildRequested, this, [this]() {
        m_albumsController->scheduleGridRebuild();
    });
    QObject::connect(this, &MainWindow::openAlbumRequested, this, [this](const Album &album, int trackIndex, bool fromTracks) {
        m_playerPageController->openAlbum(album, trackIndex, fromTracks);
    });
    QObject::connect(this, &MainWindow::openPlaylistRequested, this, [this](const Playlist &playlist, int trackIndex) {
        m_playerPageController->openPlaylist(playlist, trackIndex);
    });
    QObject::connect(this, &MainWindow::lyricsFontUpdateRequested, this, [this]() {
        m_playerPageController->updateLyricsFont();
    });
    QObject::connect(this, &MainWindow::tracksNavigationRequested, this, [this](bool tracksPage, bool playlistsPage) {
        m_tracksController->setNavigationPage(tracksPage, playlistsPage);
    });
    QObject::connect(this, &MainWindow::showPlaylistsRequested, this, [this]() {
        m_playlistsController->showPlaylistsPage();
    });
    QObject::connect(this, &MainWindow::showTracksRequested, this, [this](const QString &albumFilter) {
        m_tracksController->showTracksPage(albumFilter);
    });
    QObject::connect(this, &MainWindow::playlistsRebuildRequested, this, [this]() {
        m_playlistsController->rebuildPlaylistsGrid();
    });

        setWindowTitle("Sangeet");
        resize(1200, 800);

        m_stack = new QStackedWidget;
        setCentralWidget(m_stack);
        setAcceptDrops(true);
        qApp->installEventFilter(this);

        m_folderSelect->createStartPage();
        m_albumsController->createAlbumsPage();
        m_playlistsController->createPlaylistsPage();
        m_tracksController->createTracksPage();
        m_playerPageController->createPlayerPage();

        m_stack->addWidget(m_startPage);
        m_stack->addWidget(m_albumsPage);
        m_stack->addWidget(m_playlistsPage);
        m_stack->addWidget(m_tracksPage);
        m_stack->addWidget(m_playerPage);
        m_stack->setCurrentWidget(m_startPage);

        if (!m_playerPageController->initializeMpv()) {
            QMessageBox::critical(
                this,
                "libmpv Error",
                "Could not initialize libmpv.\n\n"
                "Install the mpv package and make sure libmpv is available."
            );
        }
        else {
            m_mpvTimer = new QTimer(this);
            m_mpvTimer->setInterval(50);
            QObject::connect(m_mpvTimer, &QTimer::timeout,
                    this, [this]() { m_playerPageController->processMpvEvents(); });
            m_mpvTimer->start();
        }

        m_mprisController->initializeMpris();

        QObject::connect(m_positionSlider, &QSlider::sliderMoved,
                this, [this](int value) {
                    m_playerPageController->seekTo(value);
                });

        QObject::connect(m_playButton, &QAbstractButton::clicked,
                this, [this]() { m_playerPageController->togglePlayback(); });

        QObject::connect(m_backToAlbums, &QPushButton::clicked,
                this, [this]() {
                    m_playerPageController->setPaused(true);
                    if (m_playbackFromPlaylist) {
                        m_playlistsController->showPlaylistsPage();
                        return;
                    }
                    if (m_playbackFromTracks) {
                        m_tracksController->showTracksPage(m_tracksAlbumFilter);
                        return;
                    }
                    m_stack->setCurrentWidget(m_albumsPage);
                    m_albumsController->scheduleGridRebuild();
                });
}

MainWindow::~MainWindow()
{
    if (m_mpv) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
    delete m_mprisController;
    delete m_playerPageController;
    delete m_tracksController;
    delete m_playlistsController;
    delete m_albumsController;
    delete m_folderSelect;
    delete m_scanning;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
        if (event->type() == QEvent::DragEnter) {
            auto *drag = static_cast<QDragEnterEvent *>(event);
            if (drag->mimeData()->hasUrls()) {
                for (const QUrl &url : drag->mimeData()->urls()) {
                    if (url.isLocalFile()) {
                        const QFileInfo info(url.toLocalFile());
                        if (info.isDir() || isMusicFile(info.absoluteFilePath()) || isPlaylistFile(info.absoluteFilePath())) {
                            drag->acceptProposedAction();
                            return true;
                        }
                    }
                }
            }
        }

        if (event->type() == QEvent::Drop) {
            auto *drop = static_cast<QDropEvent *>(event);
            if (drop->mimeData()->hasUrls()) {
                for (const QUrl &url : drop->mimeData()->urls()) {
                    if (!url.isLocalFile())
                        continue;

                    const QFileInfo info(url.toLocalFile());
                    if (info.isDir() || isMusicFile(info.absoluteFilePath()) || isPlaylistFile(info.absoluteFilePath())) {
                        drop->acceptProposedAction();
                        m_folderSelect->openPath(info.absoluteFilePath());
                        return true;
                    }
                }
            }
        }

        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const QPoint globalPos = mouse->globalPosition().toPoint();
            const auto inside = [globalPos](QWidget *widget) {
                if (!widget || !widget->isVisible())
                    return false;
                const QRect rect(widget->mapToGlobal(QPoint(0, 0)), widget->size());
                return rect.contains(globalPos);
            };

            const bool insideSearch =
                inside(m_search) || inside(m_searchButton) ||
                inside(m_playlistsSearch) || inside(m_playlistsSearchButton) ||
                inside(m_tracksSearch) || inside(m_tracksSearchButton);

            if (!insideSearch) {
                m_folderSelect->collapseSearch(m_search);
                m_folderSelect->collapseSearch(m_playlistsSearch);
                m_folderSelect->collapseSearch(m_tracksSearch);
            }
        }

        if (event->type() != QEvent::KeyPress || !m_stack)
            return QMainWindow::eventFilter(watched, event);

        auto *key = static_cast<QKeyEvent *>(event);
        
        if (key->modifiers() == Qt::ControlModifier) {
            switch (key->key()) {
                case Qt::Key_1:
                    m_tracksController->setNavigationPage(false, false);
                    m_stack->setCurrentWidget(m_albumsPage);
                    m_albumsController->scheduleGridRebuild();
                    return true;
                    
                case Qt::Key_2:
                    m_playlistsController->showPlaylistsPage();
                    return true;
                    
                case Qt::Key_3:
                    m_tracksController->showTracksPage(QString());
                    return true;
                    
                default:
                    break;
            }
        }

        if (m_stack->currentWidget() == m_tracksPage && m_tracksTable &&
            (watched == m_tracksTable || watched == m_tracksTable->viewport())) {
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                const int row = m_tracksTable->currentIndex().row();
                if (row >= 0 && row < m_trackRows.size()) {
                    m_playbackQueue = m_trackRows;
                    m_playbackQueueIndex = row;
                    m_playbackFromTracks = true;
                    const TrackRef &ref = m_playbackQueue[row];
                    m_playerPageController->openAlbum(m_albums[ref.albumIndex], ref.trackIndex, true);
                }
                return true;
            }
        }

        if (m_stack->currentWidget() == m_playlistsPage) {
            if (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right ||
                key->key() == Qt::Key_Up || key->key() == Qt::Key_Down ||
                key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                if (m_playlistsSearch && m_playlistsSearch->hasFocus())
                    return QMainWindow::eventFilter(watched, event);
                const auto cards = m_playlistsContainer->findChildren<PlaylistCard *>();
                if (cards.isEmpty())
                    return QMainWindow::eventFilter(watched, event);
                int current = 0;
                for (int i = 0; i < cards.size(); ++i) {
                    if (cards[i]->hasFocus()) { current = i; break; }
                }
                int columns = 1;
                for (int i = 0; i < m_playlistsGrid->count(); ++i) {
                    int row = 0, column = 0, rowSpan = 0, columnSpan = 0;
                    m_playlistsGrid->getItemPosition(i, &row, &column, &rowSpan, &columnSpan);
                    columns = qMax(columns, column + columnSpan);
                }
                int next = current;
                if (key->key() == Qt::Key_Left) next = qMax(0, current - 1);
                else if (key->key() == Qt::Key_Right) next = qMin(static_cast<int>(cards.size()) - 1, current + 1);
                else if (key->key() == Qt::Key_Up) next = qMax(0, current - columns);
                else if (key->key() == Qt::Key_Down) next = qMin(static_cast<int>(cards.size()) - 1, current + columns);
                else if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                    cards[current]->click();
                    return true;
                }
                cards[next]->setFocus();
                cards[next]->ensurePolished();
                return true;
            }
        }

        if (m_stack->currentWidget() == m_albumsPage) {
            if (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right ||
                key->key() == Qt::Key_Up || key->key() == Qt::Key_Down ||
                key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                if (m_search && m_search->hasFocus())
                    return QMainWindow::eventFilter(watched, event);

                const auto cards =
                    m_albumsContainer->findChildren<AlbumCard *>();
                if (cards.isEmpty())
                    return QMainWindow::eventFilter(watched, event);

                int current = 0;
                for (int i = 0; i < cards.size(); ++i) {
                    if (cards[i]->hasFocus()) {
                        current = i;
                        break;
                    }
                }

                int columns = 1;
                for (int i = 0; i < m_grid->count(); ++i) {
                    int row = 0, column = 0, rowSpan = 0, columnSpan = 0;
                    m_grid->getItemPosition(
                        i, &row, &column, &rowSpan, &columnSpan);
                    columns = qMax(columns, column + columnSpan);
                }

                int next = current;
                switch (key->key()) {
                case Qt::Key_Left:
                    next = qMax(0, current - 1);
                    break;
                case Qt::Key_Right:
                    next = qMin(static_cast<int>(cards.size()) - 1, current + 1);
                    break;
                case Qt::Key_Up:
                    next = qMax(0, current - columns);
                    break;
                case Qt::Key_Down:
                    next = qMin(static_cast<int>(cards.size()) - 1, current + columns);
                    break;
                case Qt::Key_Return:
                case Qt::Key_Enter:
                    cards[current]->click();
                    return true;
                default:
                    break;
                }

                cards[next]->setFocus();
                m_scrollArea->ensureWidgetVisible(cards[next]);
                return true;
            }
        }

        if (m_stack->currentWidget() == m_playerPage) {
            if (key->modifiers() == Qt::ControlModifier) {
                switch (key->key()) {
                    case Qt::Key_S:
                        m_playerPageController->toggleShuffle();
                        return true;
                        
                    case Qt::Key_R:
                        m_playerPageController->cycleRepeatMode();
                        return true;
                        
                    default:
                        break;
                }
            }
            switch (key->key()) {
            case Qt::Key_Up:
                m_playerPageController->previousTrack();
                return true;

            case Qt::Key_Down:
                m_playerPageController->nextTrack();
                return true;

            case Qt::Key_Left:
                m_playerPageController->seekBy(-5000);
                return true;

            case Qt::Key_Right:
                m_playerPageController->seekBy(5000);
                return true;

            case Qt::Key_Space:
                m_playerPageController->togglePlayback();
                return true;

            case Qt::Key_Escape:
                m_playerPageController->setPaused(true);
                m_stack->setCurrentWidget(m_albumsPage);
                m_albumsController->scheduleGridRebuild();
                return true;
                
            

            default:
                break;
            }
        }

        return QMainWindow::eventFilter(watched, event);
    }

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    if (m_stack->currentWidget() == m_albumsPage)
        m_albumsController->scheduleGridRebuild();

    if (m_lyrics)
        m_playerPageController->updateLyricsFont();
}
