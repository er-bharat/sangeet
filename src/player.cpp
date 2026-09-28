#include "player.h"
#include "mainwindow.h"
#include "tracks.h"
#include <algorithm>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QRandomGenerator>
#include <QSizePolicy>
#include <QIcon>
#include <QFile>
#include <QFileInfo>
#include <QToolButton>
#include <QLabel>
#include <QSlider>
#include <QEnterEvent>
#include <QListWidgetItem>

#include <algorithm>
#include <clocale>

#include <mpv/client.h>

#include <taglib/fileref.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/tpropertymap.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mp4item.h>

#include <QMouseEvent>
#include <QPainter>
#include <QStackedWidget>
#include <functional>
#include "loudness_graph.h"

#include <QMetaObject>
#include <QThreadPool>
#include <QMutexLocker>
PlayerArtWidget::PlayerArtWidget(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);

    m_art = new QLabel(this);
    m_art->setAlignment(Qt::AlignCenter);
    m_art->setStyleSheet("background: black;");

    m_tracks = new QListWidget(this);
    m_tracks->setStyleSheet(
        "QListWidget { background: rgba(0,0,0,220); color: white; border: none; padding: 6px; }"
        "QListWidget::item { padding: 8px; }"
        "QListWidget::item:selected { background: rgba(255,255,255,45); }");
    m_tracks->hide();
    m_tracks->setMouseTracking(true);

    m_shuffleButton = new QToolButton(this);
    m_repeatButton = new QToolButton(this);
    for (QToolButton *button : {m_shuffleButton, m_repeatButton}) {
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setIconSize(QSize(22, 22));
        button->setStyleSheet(
            "QToolButton { background: rgba(0,0,0,180); border: none; border-radius: 6px; padding: 6px; }"
            "QToolButton:hover { background: rgba(255,255,255,45); }");
    }
    m_shuffleButton->setIcon(QIcon::fromTheme("media-playlist-shuffle", QIcon::fromTheme("media-playlist-shuffle-symbolic")));
    m_repeatButton->setIcon(QIcon::fromTheme("media-playlist-repeat", QIcon::fromTheme("media-playlist-repeat-symbolic")));
    m_shuffleButton->setToolTip("Shuffle: Off");
    m_repeatButton->setToolTip("Repeat: Off");
    m_shuffleButton->hide();
    m_repeatButton->hide();

    connect(m_shuffleButton, &QToolButton::clicked, this, &PlayerArtWidget::shuffleClicked);
    connect(m_repeatButton, &QToolButton::clicked, this, &PlayerArtWidget::repeatClicked);
    connect(&m_hideTimer, &QTimer::timeout, this, [this]() {
        if (!underMouse() && !m_tracks->underMouse())
            hideOverlays();
    });
    m_hideTimer.setSingleShot(true);

    connect(m_tracks, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        emit trackSelected(item->data(Qt::UserRole).toInt());
    });
}

void PlayerArtWidget::setArtwork(const QString &path)
{
    m_pixmap = QPixmap(path);
    updateArtwork();
}

void PlayerArtWidget::setStreamFallback(bool stream)
{
    m_streamFallback = stream;
    updateArtwork();
}

void PlayerArtWidget::setTracks(const QList<Track> &tracks)
{
    m_tracks->clear();
    for (int i = 0; i < tracks.size(); ++i) {
        const Track &track = tracks[i];
        QString text;
        if (track.trackNumber > 0)
            text += QString::number(track.trackNumber) + ". ";
        text += track.title;
        auto *item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, i);
        m_tracks->addItem(item);
    }
}

void PlayerArtWidget::setCurrentTrack(int index)
{
    if (index >= 0 && index < m_tracks->count())
        m_tracks->setCurrentRow(index);
}

void PlayerArtWidget::showTrackListTemporarily()
{
    if (m_tracks->count() == 0)
        return;
    showOverlays();
    m_hideTimer.start(1500);
}

void PlayerArtWidget::setShuffleState(bool enabled)
{
    m_shuffleButton->setToolTip(enabled ? "Shuffle: On" : "Shuffle: Off");
    m_shuffleButton->setStyleSheet(enabled
        ? "QToolButton { background: rgba(77,163,255,180); border: none; border-radius: 6px; padding: 6px; } QToolButton:hover { background: rgba(77,163,255,220); }"
        : "QToolButton { background: rgba(0,0,0,180); border: none; border-radius: 6px; padding: 6px; } QToolButton:hover { background: rgba(255,255,255,45); }");
}

void PlayerArtWidget::setRepeatMode(int mode)
{
    QString tooltip;
    QString iconName = "media-playlist-repeat";
    if (mode == 1) tooltip = "Repeat: All";
    else if (mode == 2) { tooltip = "Repeat: One"; iconName = "media-playlist-repeat-song"; }
    else tooltip = "Repeat: Off";

    QIcon icon = QIcon::fromTheme(iconName);
    if (icon.isNull()) icon = QIcon::fromTheme("media-playlist-repeat");
    m_repeatButton->setIcon(icon);
    m_repeatButton->setToolTip(tooltip);
    m_repeatButton->setStyleSheet(mode != 0
        ? "QToolButton { background: rgba(77,163,255,180); border: none; border-radius: 6px; padding: 6px; } QToolButton:hover { background: rgba(77,163,255,220); }"
        : "QToolButton { background: rgba(0,0,0,180); border: none; border-radius: 6px; padding: 6px; } QToolButton:hover { background: rgba(255,255,255,45); }");
}

void PlayerArtWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateArtwork();
    updateGeometry();
}

void PlayerArtWidget::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    m_hideTimer.stop();
    showOverlays();
    raise();
}

void PlayerArtWidget::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (!m_tracks->underMouse() && !m_shuffleButton->underMouse() && !m_repeatButton->underMouse())
        hideOverlays();
}

void PlayerArtWidget::updateArtwork()
{
    if (width() <= 0 || height() <= 0)
        return;

    const int side = qMin(width(), height());
    const QRect rect((width() - side) / 2, (height() - side) / 2, side, side);
    m_art->setGeometry(rect);
    m_tracks->setGeometry(rect.x(), rect.y() + rect.height() / 2, rect.width(), rect.height() / 2);

    const int buttonSize = 34;
    const int margin = qMax(8, rect.width() / 60);
    m_shuffleButton->setGeometry(rect.x() + margin, rect.y() + margin, buttonSize, buttonSize);
    m_repeatButton->setGeometry(rect.right() - buttonSize - margin, rect.y() + margin, buttonSize, buttonSize);

    if (!m_pixmap.isNull()) {
        m_art->setPixmap(m_pixmap.scaled(rect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        return;
    }

    if (m_streamFallback) {
        QIcon radioIcon = QIcon::fromTheme(QStringLiteral("radio"));
        if (radioIcon.isNull()) radioIcon = QIcon::fromTheme(QStringLiteral("audio-x-generic"));
        if (!radioIcon.isNull()) {
            const int iconSide = qMin(rect.width(), rect.height()) / 3;
            m_art->setPixmap(radioIcon.pixmap(QSize(iconSide, iconSide)));
        } else {
            m_art->setText(QStringLiteral("📻"));
            QFont font = m_art->font();
            font.setPointSize(qMax(24, side / 5));
            m_art->setFont(font);
        }
        return;
    }
    m_art->clear();
}

void PlayerArtWidget::showOverlays()
{
    if (m_tracks->count() > 0) m_tracks->show();
    m_shuffleButton->show();
    m_repeatButton->show();
    m_tracks->raise();
    m_shuffleButton->raise();
    m_repeatButton->raise();
}

void PlayerArtWidget::hideOverlays()
{
    m_tracks->hide();
    m_shuffleButton->hide();
    m_repeatButton->hide();
}

static QStringList embeddedLyricTexts(const QString &musicPath)
{
    QStringList lyrics;
    TagLib::FileRef file(musicPath.toUtf8().constData());
    if (file.isNull() || !file.file()) return lyrics;

    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file.file())) {
        if (auto *tag = mpeg->ID3v2Tag(false)) {
            const auto map = tag->frameListMap();
            auto it = map.find("USLT");
            if (it != map.end()) {
                for (TagLib::ID3v2::Frame *frame : it->second) {
                    auto *lyricsFrame = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame *>(frame);
                    if (!lyricsFrame) continue;
                    const QString text = QString::fromUtf8(lyricsFrame->text().toCString(true)).trimmed();
                    if (!text.isEmpty()) lyrics.append(text);
                }
            }
        }
    }

    const TagLib::PropertyMap properties = file.file()->properties();
    const QStringList keys = {"LYRICS", "UNSYNCEDLYRICS", "UNSYNCED LYRICS", "LYRICS3"};
    for (const QString &key : keys) {
        auto it = properties.find(key.toStdString());
        if (it == properties.end()) continue;
        for (const TagLib::String &value : it->second) {
            const QString text = QString::fromUtf8(value.toCString(true)).trimmed();
            if (!text.isEmpty()) lyrics.append(text);
        }
    }

    if (auto *mp4 = dynamic_cast<TagLib::MP4::File *>(file.file())) {
        if (auto *tag = mp4->tag()) {
            const auto items = tag->itemMap();
            auto it = items.find("\xC2\xA9lyr");
            if (it != items.end()) {
                const TagLib::StringList values = it->second.toStringList();
                for (const TagLib::String &value : values) {
                    const QString text = QString::fromUtf8(value.toCString(true)).trimmed();
                    if (!text.isEmpty()) lyrics.append(text);
                }
            }
        }
    }
    return lyrics;
}

static QList<LyricLine> parseLyricText(const QString &text)
{
    QList<LyricLine> result;
    const QStringList lines = text.split('\n');
    static const QRegularExpression expression(R"(\[(\d+):(\d{2})(?:[.:](\d{1,3}))?\](.*))");
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;
        const QRegularExpressionMatch match = expression.match(line);
        if (!match.hasMatch()) continue;
        const int minutes = match.captured(1).toInt();
        const int seconds = match.captured(2).toInt();
        QString fraction = match.captured(3);
        while (fraction.size() < 3) fraction += '0';
        if (fraction.size() > 3) fraction = fraction.left(3);
        LyricLine lyric;
        lyric.position = minutes * 60000LL + seconds * 1000LL + (fraction.isEmpty() ? 0 : fraction.toInt());
        lyric.text = match.captured(4).trimmed();
        if (!lyric.text.isEmpty()) result.append(lyric);
    }
    std::sort(result.begin(), result.end(), [](const LyricLine &a, const LyricLine &b) { return a.position < b.position; });
    return result;
}

static QList<LyricLine> plainLyrics(const QString &text)
{
    QList<LyricLine> result;

    for (const QString &line : text.split('\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        
        result.append({-1, trimmed});
    }
    
    return result;
}

static QList<LyricLine> externalLyrics(const QString &musicPath)
{
    const QFileInfo info(musicPath);
    QFile file(info.absolutePath() + "/" + info.completeBaseName() + ".lrc");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return parseLyricText(QString::fromUtf8(file.readAll()));
}

static QList<LyricLine> loadLyrics(const QString &musicPath)
{
    const QStringList embedded = embeddedLyricTexts(musicPath);
    for (const QString &text : embedded) {
        const auto parsed = parseLyricText(text);
        if (!parsed.isEmpty()) return parsed;
    }
    if (!embedded.isEmpty()) {
        const auto parsed = plainLyrics(embedded.first());
        if (!parsed.isEmpty()) return parsed;
    }
    return externalLyrics(musicPath);
}

class ClickableTimeLabel : public QLabel
{
public:
    using QLabel::QLabel;
    std::function<void()> onClick;
    
protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onClick)
            onClick();
        QLabel::mousePressEvent(event);
    }
};

class LoudnessProgressWidget : public QWidget
{
public:
    explicit LoudnessProgressWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(28);
        setCursor(Qt::PointingHandCursor);
    }
    
    // Peak is the max-per-bucket value from LoudnessGraph::Result::peak.
    void setData(const std::vector<double> &peak)
    {
        m_peak = peak;
        
        const double maxPeak = m_peak.empty()
        ? 0.0
        : *std::max_element(m_peak.begin(), m_peak.end());
        
        if (maxPeak > 1e-6) {
            const double gain = 1.0 / maxPeak;
            for (double &v : m_peak)
                v = qBound(0.0, v * gain, 1.0);
        }
        
        update();
    }
    
    void setRange(qint64 duration) { m_duration = duration; update(); }
    void setPosition(qint64 position) { m_position = position; update(); }
    
    std::function<void(int)> onSeek;
    
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        
        if (m_peak.empty())
            return;
        
        const int count = static_cast<int>(m_peak.size());
        const double step = width() / static_cast<double>(count);
        const double barWidth = qMax(1.0, step - 1.0);
        
        const double progress = m_duration > 0
        ? static_cast<double>(m_position) / static_cast<double>(m_duration)
        : 0.0;
        
        p.setPen(Qt::NoPen);
        
        for (int i = 0; i < count; ++i) {
            const bool played = i / static_cast<double>(count) <= progress;
            const double x = i * step;
            
            const double barHeight =
            qMax(2.0, height() * m_peak[static_cast<size_t>(i)]);
            
            p.setBrush(barGradient(played));
            p.drawRoundedRect(
                QRectF(x, (height() - barHeight) / 2.0, barWidth, barHeight),
                              1, 1);
        }
    }
    
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || m_duration <= 0 || !onSeek)
            return;
        
        const double ratio = event->position().x() / static_cast<double>(width());
        const qint64 target = qRound64(ratio * static_cast<double>(m_duration));
        
        onSeek(static_cast<int>(target));
    }
    
private:
    // Top-to-bottom gradient for the bar -- flat fill reads as a
    // placeholder, a gradient reads as designed. Played columns tint
    // toward the highlight color, unplayed toward mid.
    QLinearGradient barGradient(bool played) const
    {
        QLinearGradient gradient(0, 0, 0, height());
        
        const QColor base = palette().color(played ? QPalette::Highlight : QPalette::Mid);
        gradient.setColorAt(0.0, base.lighter(135));
        gradient.setColorAt(0.5, base);
        gradient.setColorAt(1.0, base.darker(115));
        
        return gradient;
    }
    
    std::vector<double> m_peak;
    qint64 m_duration = 0;
    qint64 m_position = 0;
};

PlayerPage::PlayerPage(MainWindow *window) : m_window(window) {}

void PlayerPage::createPlayerHeader(QGridLayout *top)
{
    m_window->m_backToAlbums = new QPushButton("← All Albums");
    top->addWidget(m_window->m_backToAlbums, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    
    auto *center = new QWidget;
    auto *layout = new QVBoxLayout(center);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    m_window->m_trackTitle = new QLabel;
    m_window->m_trackTitle->setAlignment(Qt::AlignCenter);
    
    QFont titleFont = m_window->m_trackTitle->font();
    titleFont.setPointSize(14);
    titleFont.setBold(false);
    m_window->m_trackTitle->setFont(titleFont);
    
    m_window->m_trackArtist = new QLabel;
    m_window->m_trackArtist->setAlignment(Qt::AlignCenter);
    
    QFont artistFont = m_window->m_trackArtist->font();
    artistFont.setPointSize(11);
    artistFont.setBold(false);
    m_window->m_trackArtist->setFont(artistFont);
    
    layout->addWidget(m_window->m_trackTitle);
    layout->addWidget(m_window->m_trackArtist);
    
    top->addWidget(center, 0, 1, Qt::AlignCenter);
    
    QFont albumFont = m_window->m_trackTitle->font();
    albumFont.setPointSize(16);
    albumFont.setBold(true);
    
    m_window->m_albumName = new QToolButton;
    m_window->m_albumName->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_window->m_albumName->setAutoRaise(true);
    m_window->m_albumName->setCursor(Qt::PointingHandCursor);
    m_window->m_albumName->setFont(albumFont);
    top->addWidget(m_window->m_albumName, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
}

void PlayerPage::createLyrics(QHBoxLayout *content)
{
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(20, 0, 20, 0);
    layout->setSpacing(8);
    m_window->m_lyrics = new QListWidget;
    m_window->m_lyrics->setFocusPolicy(Qt::NoFocus);
    m_window->m_lyrics->setSelectionMode(QAbstractItemView::SingleSelection);
    m_window->m_lyrics->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_window->m_lyrics->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_window->m_lyrics->setWordWrap(true);
    m_window->m_lyrics->setTextElideMode(Qt::ElideNone);
    m_window->m_lyrics->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_window->m_lyrics->verticalScrollBar()->setSingleStep(8);
    m_window->m_lyrics->setAttribute(Qt::WA_TranslucentBackground);
    m_window->m_lyrics->setStyleSheet(R"(
    QListWidget {
        background: transparent;
        border: none;
        outline: none;
        padding: 8px 4px;
    }

    QListWidget::item {
        background: transparent;
        border: none;
        padding: 8px 4px;
    }

    QScrollBar:vertical {
        width: 8px;
        margin: 0;
        background: transparent;
    }

    QScrollBar::handle:vertical {
        min-height: 30px;
        background: rgba(150,150,150,140);
        border-radius: 4px;
    }

    QScrollBar::add-line:vertical,
    QScrollBar::sub-line:vertical,
    QScrollBar::add-page:vertical,
    QScrollBar::sub-page:vertical {
        background: transparent;
        height: 0;
    }
    )");
    emit m_window->lyricsFontUpdateRequested();
    layout->addWidget(m_window->m_lyrics, 1);
    content->addWidget(panel, 1);
}

void PlayerPage::createPlayerControls(QVBoxLayout *root)
{
    auto *controls = new QHBoxLayout;
    
    auto *timeLabel = new ClickableTimeLabel("0:00");
    m_window->m_currentTime = timeLabel;
    timeLabel->setToolTip("waveform/normal bar");
    timeLabel->onClick = [this]() {
        const int mode = m_progressStack->currentIndex() == 0 ? 1 : 0;
        m_progressStack->setCurrentIndex(mode);
        m_window->m_settings.saveProgressBarMode(mode);
    };
    
    m_window->m_duration = new QLabel("0:00");
    
    QFont font = m_window->m_currentTime->font();
    font.setPointSize(12);
    
    m_window->m_currentTime->setFont(font);
    m_window->m_duration->setFont(font);
    
    const int width = QFontMetrics(font).horizontalAdvance("00:00:00") + 6;
    
    m_window->m_currentTime->setFixedWidth(width);
    m_window->m_duration->setFixedWidth(width);
    
    m_window->m_currentTime->setAlignment(Qt::AlignCenter);
    m_window->m_duration->setAlignment(Qt::AlignCenter);
    
    m_window->m_positionSlider = new QSlider(Qt::Horizontal);
    
    m_waveform = new LoudnessProgressWidget;
    m_progressStack = new QStackedWidget;
    
    m_progressStack->addWidget(m_window->m_positionSlider);
    m_progressStack->addWidget(m_waveform);
    
    m_progressStack->setCurrentIndex(
        qBound(0, m_window->m_settings.progressBarMode(), 1));
    
    m_waveform->onSeek = [this](int position) {
        seekTo(position);
    };
    
    m_window->m_playButton = new QToolButton;
    m_window->m_playButton->setIcon(QIcon::fromTheme("media-playback-start"));
    m_window->m_playButton->setToolTip("Play");
    m_window->m_playButton->setIconSize(QSize(24, 24));
    m_window->m_playButton->setAutoRaise(true);
    
    controls->addWidget(m_window->m_currentTime);
    controls->addWidget(m_progressStack, 1);
    controls->addWidget(m_window->m_duration);
    controls->addWidget(m_window->m_playButton);
    
    root->addLayout(controls);
}

void PlayerPage::connectPlayerSignals()
{
    QObject::connect(m_window->m_artWidget, &PlayerArtWidget::trackSelected,
                     m_window, [this](int index) {
                         if (m_window->m_playbackFromPlaylist) {
                             if (index >= 0 && index < m_window->m_playbackQueue.size())
                                 playPlaylistQueueTrack(index, m_window->m_currentPlaylist);
                         } else if (index >= 0 && index < m_window->m_currentAlbum.tracks.size()) {
                             playTrack(index);
                         }
                     });
    
    QObject::connect(m_window->m_artWidget, &PlayerArtWidget::shuffleClicked,
                     m_window, [this]() {
                         toggleShuffle();
                     });
    
    QObject::connect(m_window->m_artWidget, &PlayerArtWidget::repeatClicked,
                     m_window, [this]() {
                         cycleRepeatMode();
                     });
    
    QObject::connect(m_window->m_albumName, &QToolButton::clicked,
                     m_window, [this]() {
                         if (m_window->m_playbackFromPlaylist)
                             emit m_window->showPlaylistsRequested();
                         else
                             m_window->m_tracksController->showTracksPage(m_window->m_currentAlbum.name);
                     });
}

void PlayerPage::createPlayerPage()
{
    m_window->m_playerPage = new QWidget;
    auto *root = new QVBoxLayout(m_window->m_playerPage);
    root->setContentsMargins(15, 15, 15, 15);
    root->setSpacing(10);
    auto *top = new QGridLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setColumnStretch(0, 1);
    top->setColumnStretch(1, 1);
    top->setColumnStretch(2, 1);
    createPlayerHeader(top);
    root->addLayout(top);
    auto *content = new QHBoxLayout;
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(0);
    m_window->m_artWidget = new PlayerArtWidget;
    m_window->m_artWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    content->addWidget(m_window->m_artWidget, 1);
    createLyrics(content);
    root->addLayout(content, 1);
    createPlayerControls(root);
    connectPlayerSignals();
    m_window->m_artWidget->setShuffleState(false);
    m_window->m_artWidget->setRepeatMode(0);
}

void PlayerPage::openPlaylist(const Playlist &playlist, int trackIndex)
{
    if (playlist.tracks.isEmpty() || playlist.refs.isEmpty()) return;
    m_window->m_playbackQueue.clear();
    for (const auto &ref : playlist.refs) m_window->m_playbackQueue.append({ref.first, ref.second});
    m_window->m_sequentialPlaybackQueue = m_window->m_playbackQueue;
    m_window->m_playbackQueueIndex = qBound(0, trackIndex, static_cast<int>(m_window->m_playbackQueue.size()) - 1);
    m_window->m_playbackFromTracks = false;
    m_window->m_playbackFromPlaylist = true;
    m_window->m_currentPlaylist = playlist;
    playPlaylistQueueTrack(m_window->m_playbackQueueIndex, playlist);
}

void PlayerPage::playPlaylistQueueTrack(int queueIndex, const Playlist &playlist)
{
    if (queueIndex < 0 || queueIndex >= m_window->m_playbackQueue.size()) return;
    const TrackRef &ref = m_window->m_playbackQueue[queueIndex];
    if (ref.trackIndex < 0 || ref.trackIndex >= playlist.tracks.size()) return;
    const Track &track = playlist.tracks[ref.trackIndex];
    m_window->m_playbackQueueIndex = queueIndex;
    m_window->m_currentTrack = 0;
    m_window->m_stack->setCurrentWidget(m_window->m_playerPage);
    m_window->m_backToAlbums->setText("← Playlists");
    m_window->m_albumName->setText(playlist.name);
    m_window->m_artWidget->setTracks(playlist.tracks);
    m_window->m_artWidget->setCurrentTrack(queueIndex);
    if (ref.albumIndex >= 0 && ref.albumIndex < m_window->m_albums.size() && ref.trackIndex >= 0 && ref.trackIndex < m_window->m_albums[ref.albumIndex].tracks.size()) {
        m_window->m_currentAlbum = m_window->m_albums[ref.albumIndex];
        m_window->m_currentTrack = ref.trackIndex;
        m_window->m_artWidget->setArtwork(m_window->m_currentAlbum.artwork);
    } else {
        m_window->m_currentAlbum = Album{};
        m_window->m_currentAlbum.name = playlist.name;
        m_window->m_currentAlbum.tracks = {track};
        m_window->m_artWidget->setArtwork(QString());
    }
    m_window->m_artWidget->setStreamFallback(track.path.startsWith("http://", Qt::CaseInsensitive) || track.path.startsWith("https://", Qt::CaseInsensitive));
    playTrack(m_window->m_currentTrack);
}

void PlayerPage::openAlbum(const Album &album, int trackIndex, bool fromTracks)
{
    if (album.tracks.isEmpty()) return;
    trackIndex = qBound(0, trackIndex, static_cast<int>(album.tracks.size()) - 1);
    m_window->m_playbackFromTracks = fromTracks;
    m_window->m_playbackFromPlaylist = false;
    if (!fromTracks) {
        m_window->m_playbackQueue.clear();
        m_window->m_sequentialPlaybackQueue.clear();
        m_window->m_playbackQueueIndex = -1;
    }
    m_window->m_currentAlbum = album;
    m_window->m_currentTrack = trackIndex;
    m_window->m_stack->setCurrentWidget(m_window->m_playerPage);
    m_window->m_backToAlbums->setText(fromTracks ? "← Tracks" : "← All Albums");
    m_window->m_albumName->setText(album.name.isEmpty() ? "Unknown Album" : album.name);
    m_window->m_artWidget->setArtwork(album.artwork);
    m_window->m_artWidget->setStreamFallback(false);
    m_window->m_artWidget->setTracks(album.tracks);
    playTrack(trackIndex);
}

void PlayerPage::playQueueTrack(int queueIndex)
{
    if (queueIndex < 0 || queueIndex >= m_window->m_playbackQueue.size()) return;
    const TrackRef &ref = m_window->m_playbackQueue[queueIndex];
    if (ref.albumIndex < 0 || ref.albumIndex >= m_window->m_albums.size()) return;
    const Album &album = m_window->m_albums[ref.albumIndex];
    if (ref.trackIndex < 0 || ref.trackIndex >= album.tracks.size()) return;
    m_window->m_playbackQueueIndex = queueIndex;
    m_window->m_playbackFromTracks = true;
    m_window->m_playbackFromPlaylist = false;
    m_window->m_currentAlbum = album;
    m_window->m_currentTrack = ref.trackIndex;
    m_window->m_stack->setCurrentWidget(m_window->m_playerPage);
    m_window->m_backToAlbums->setText("← Tracks");
    m_window->m_albumName->setText(album.name.isEmpty() ? "Unknown Album" : album.name);
    m_window->m_artWidget->setArtwork(album.artwork);
    m_window->m_artWidget->setStreamFallback(false);
    m_window->m_artWidget->setTracks(album.tracks);
    playTrack(ref.trackIndex);
}

void PlayerPage::nextTrack()
{
    if (m_window->m_playbackFromPlaylist || m_window->m_playbackFromTracks) {
        if (m_window->m_playbackQueue.isEmpty()) return;
        if (m_window->m_playbackQueueIndex + 1 < m_window->m_playbackQueue.size()) {
            const int next = m_window->m_playbackQueueIndex + 1;
            if (m_window->m_playbackFromPlaylist) playPlaylistQueueTrack(next, m_window->m_currentPlaylist);
            else playQueueTrack(next);
        } else if (m_window->m_repeatMode == 1) {
            if (m_window->m_playbackFromPlaylist) playPlaylistQueueTrack(0, m_window->m_currentPlaylist);
            else playQueueTrack(0);
        }
        return;
    }
    if (m_window->m_albumPlaybackOrder.isEmpty()) {
        if (m_window->m_currentTrack + 1 < m_window->m_currentAlbum.tracks.size()) playTrack(m_window->m_currentTrack + 1);
        else if (m_window->m_repeatMode == 1) playTrack(0);
        return;
    }
    const qsizetype position = m_window->m_albumPlaybackOrder.indexOf(m_window->m_currentTrack);
    if (position >= 0 && position + 1 < m_window->m_albumPlaybackOrder.size()) playTrack(m_window->m_albumPlaybackOrder[position + 1]);
    else if (m_window->m_repeatMode == 1) playTrack(m_window->m_albumPlaybackOrder.first());
}

void PlayerPage::previousTrack()
{
    if (m_window->m_playbackFromPlaylist || m_window->m_playbackFromTracks) {
        if (m_window->m_playbackQueue.isEmpty()) return;
        if (m_window->m_playbackQueueIndex > 0) {
            const int previous = m_window->m_playbackQueueIndex - 1;
            if (m_window->m_playbackFromPlaylist) playPlaylistQueueTrack(previous, m_window->m_currentPlaylist);
            else playQueueTrack(previous);
        } else if (m_window->m_repeatMode == 1) {
            const int last = static_cast<int>(m_window->m_playbackQueue.size() - 1);
            if (m_window->m_playbackFromPlaylist) playPlaylistQueueTrack(last, m_window->m_currentPlaylist);
            else playQueueTrack(last);
        }
        return;
    }
    if (m_window->m_albumPlaybackOrder.isEmpty()) {
        if (m_window->m_currentTrack > 0) playTrack(m_window->m_currentTrack - 1);
        else if (m_window->m_repeatMode == 1 && !m_window->m_currentAlbum.tracks.isEmpty()) playTrack(static_cast<int>(m_window->m_currentAlbum.tracks.size() - 1));
        return;
    }
    const qsizetype position = m_window->m_albumPlaybackOrder.indexOf(m_window->m_currentTrack);
    if (position > 0) playTrack(m_window->m_albumPlaybackOrder[position - 1]);
    else if (m_window->m_repeatMode == 1) playTrack(m_window->m_albumPlaybackOrder.last());
}

void PlayerPage::shuffleQueue()
{
    const TrackRef current = m_window->m_playbackQueue[m_window->m_playbackQueueIndex];
    QList<TrackRef> shuffled = m_window->m_sequentialPlaybackQueue;
    for (qsizetype i = shuffled.size() - 1; i > 0; --i)
        shuffled.swapItemsAt(i, QRandomGenerator::global()->bounded(static_cast<int>(i + 1)));
    shuffled.removeIf([current](const TrackRef &ref) { return ref.albumIndex == current.albumIndex && ref.trackIndex == current.trackIndex; });
    shuffled.prepend(current);
    m_window->m_playbackQueue = shuffled;
    m_window->m_playbackQueueIndex = 0;
}

void PlayerPage::restoreQueue()
{
    const TrackRef current = m_window->m_playbackQueue[m_window->m_playbackQueueIndex];
    m_window->m_playbackQueue = m_window->m_sequentialPlaybackQueue;
    m_window->m_playbackQueueIndex = -1;
    for (int i = 0; i < m_window->m_playbackQueue.size(); ++i) {
        if (m_window->m_playbackQueue[i].albumIndex == current.albumIndex && m_window->m_playbackQueue[i].trackIndex == current.trackIndex) {
            m_window->m_playbackQueueIndex = i;
            break;
        }
    }
}

void PlayerPage::shuffleAlbum()
{
    m_window->m_albumPlaybackOrder.clear();
    if (!m_window->m_shuffleEnabled || m_window->m_currentAlbum.tracks.size() <= 1) return;
    for (int i = 0; i < m_window->m_currentAlbum.tracks.size(); ++i) m_window->m_albumPlaybackOrder.append(i);
    for (qsizetype i = m_window->m_albumPlaybackOrder.size() - 1; i > 0; --i)
        m_window->m_albumPlaybackOrder.swapItemsAt(i, QRandomGenerator::global()->bounded(static_cast<int>(i + 1)));
    const qsizetype current = m_window->m_albumPlaybackOrder.indexOf(m_window->m_currentTrack);
    if (current > 0) m_window->m_albumPlaybackOrder.swapItemsAt(0, current);
}

void PlayerPage::toggleShuffle()
{
    m_window->m_shuffleEnabled = !m_window->m_shuffleEnabled;
    if (m_window->m_playbackFromPlaylist || m_window->m_playbackFromTracks) {
        if (m_window->m_shuffleEnabled) shuffleQueue();
        else restoreQueue();
    } else {
        shuffleAlbum();
    }
    m_window->m_artWidget->setShuffleState(m_window->m_shuffleEnabled);
    m_window->m_artWidget->showTrackListTemporarily();
}

void PlayerPage::cycleRepeatMode()
{
    m_window->m_repeatMode = (m_window->m_repeatMode + 1) % 3;
    m_window->m_artWidget->setRepeatMode(m_window->m_repeatMode);
    m_window->m_artWidget->showTrackListTemporarily();
}

void PlayerPage::playTrack(int index)
{
    if (index < 0 || index >= m_window->m_currentAlbum.tracks.size()) return;
    
    m_window->m_currentTrack = index;
    m_window->m_trackEnded = false;
    
    const Track &track = m_window->m_currentAlbum.tracks[index];
    m_currentLoudnessPath = track.path;
    
    m_window->m_trackTitle->setText(track.title);
    m_window->m_trackArtist->setText(track.artist);
    m_window->m_artWidget->setCurrentTrack(
        m_window->m_playbackFromPlaylist
        ? m_window->m_playbackQueueIndex
        : index);
    m_window->m_artWidget->showTrackListTemporarily();
    
    m_window->m_lyricsData = loadLyrics(track.path);
    populateLyrics();
    
    if (m_waveform) {
        showLoudness(track.path);
        requestLoudness(track.path);
    }
    
    preloadLoudness();
    
    const QByteArray path = track.path.toUtf8();
    const char *command[] = {"loadfile", path.constData(), "replace", nullptr};
    mpv_command_async(m_window->m_mpv, 0, command);
    
    int pause = 0;
    mpv_set_property(m_window->m_mpv, "pause", MPV_FORMAT_FLAG, &pause);
    
    m_window->m_isPlaying = true;
    m_window->m_playButton->setIcon(
        QIcon::fromTheme("media-playback-pause"));
    m_window->m_playButton->setToolTip("Pause");
}

void PlayerPage::requestLoudness(const QString &path)
{
    if (path.isEmpty() ||
        path.startsWith("http://", Qt::CaseInsensitive) ||
        path.startsWith("https://", Qt::CaseInsensitive))
        return;
    
    {
        QMutexLocker lock(&m_loudnessMutex);
        
        if (m_loudnessCache.contains(path) ||
            m_loudnessPending.contains(path))
            return;
        
        m_loudnessPending.insert(path);
    }
    
    QThreadPool::globalInstance()->start([this, path]() {
        LoudnessGraph::Result result;
        std::string error;
        
        const bool ok =
        LoudnessGraph::analyzeFile(
            path.toStdString(),
                                   result,
                                   error);
        
        QMetaObject::invokeMethod(
            m_window,
            [this, path, ok, rms = std::move(result.rms)]() mutable {
                {
                    QMutexLocker lock(&m_loudnessMutex);
                    
                    m_loudnessPending.remove(path);
                    
                    if (ok && !rms.empty())
                        m_loudnessCache.insert(
                            path,
                            std::move(rms));
                }
                
                showLoudness(path);
            },
            Qt::QueuedConnection);
    });
}

void PlayerPage::preloadLoudness()
{
    auto request = [this](const Track &track) {
        requestLoudness(track.path);
    };
    
    if (m_window->m_playbackFromPlaylist ||
        m_window->m_playbackFromTracks) {
        
        const int center =
        m_window->m_playbackQueueIndex;
    
    for (int offset = -2; offset <= 2; ++offset) {
        const int index = center + offset;
        
        if (index < 0 ||
            index >= m_window->m_playbackQueue.size())
            continue;
        
        const TrackRef &ref =
        m_window->m_playbackQueue[index];
        
        if (m_window->m_playbackFromTracks) {
            if (ref.albumIndex < 0 ||
                ref.albumIndex >= m_window->m_albums.size())
                continue;
            
            const Album &album =
            m_window->m_albums[ref.albumIndex];
            
            if (ref.trackIndex >= 0 &&
                ref.trackIndex < album.tracks.size())
                request(album.tracks[ref.trackIndex]);
        }
        else if (ref.albumIndex >= 0 &&
            ref.albumIndex < m_window->m_albums.size() &&
            ref.trackIndex >= 0 &&
            ref.trackIndex <
            m_window->m_albums[ref.albumIndex].tracks.size()) {
            
            request(
                m_window->m_albums[ref.albumIndex]
                .tracks[ref.trackIndex]);
            }
            else if (ref.trackIndex >= 0 &&
                ref.trackIndex <
                m_window->m_currentPlaylist.tracks.size()) {
                
                request(
                    m_window->m_currentPlaylist
                    .tracks[ref.trackIndex]);
                }
    }
    
    return;
        }
        
        const QList<int> &order =
        m_window->m_albumPlaybackOrder;
        
        if (order.isEmpty()) {
            for (int offset = -2; offset <= 2; ++offset) {
                const int index =
                m_window->m_currentTrack + offset;
                
                if (index >= 0 &&
                    index < m_window->m_currentAlbum.tracks.size())
                    request(
                        m_window->m_currentAlbum.tracks[index]);
            }
            
            return;
        }
        
        const qsizetype center =
        order.indexOf(m_window->m_currentTrack);
        
        if (center < 0)
            return;
    
    for (qsizetype offset = -2; offset <= 2; ++offset) {
        const qsizetype index = center + offset;
        
        if (index >= 0 && index < order.size())
            request(
                m_window->m_currentAlbum
                .tracks[order[index]]);
    }
}

void PlayerPage::showLoudness(const QString &path)
{
    if (!m_waveform || path != m_currentLoudnessPath)
        return;
    
    QMutexLocker lock(&m_loudnessMutex);
    
    const auto it = m_loudnessCache.constFind(path);
    
    if (it != m_loudnessCache.constEnd())
        m_waveform->setData(it.value());
    else
        m_waveform->setData({});
    
    m_waveform->setPosition(0);
}

void PlayerPage::togglePlayback()
{
    if (!m_window->m_mpv) return;
    if (!m_window->m_isPlaying && m_window->m_trackEnded) {
        playTrack(m_window->m_currentTrack);
        return;
    }
    setPaused(m_window->m_isPlaying);
}

void PlayerPage::setPaused(bool paused)
{
    if (!m_window->m_mpv) return;
    int value = paused ? 1 : 0;
    mpv_set_property(m_window->m_mpv, "pause", MPV_FORMAT_FLAG, &value);
    m_window->m_isPlaying = !paused;
    if (m_window->m_lyrics) m_window->m_lyrics->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_window->m_playButton->setIcon(QIcon::fromTheme(m_window->m_isPlaying ? "media-playback-pause" : "media-playback-start"));
    m_window->m_playButton->setToolTip(m_window->m_isPlaying ? "Pause" : "Play");
}

void PlayerPage::seekTo(qint64 milliseconds)
{
    if (!m_window->m_mpv) return;
    double seconds = static_cast<double>(milliseconds) / 1000.0;
    mpv_set_property(m_window->m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &seconds);
}

void PlayerPage::seekBy(double milliseconds)
{
    if (!m_window->m_mpv) return;
    double position = 0.0;
    if (mpv_get_property(m_window->m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &position) < 0) return;
    double duration = 0.0;
    if (mpv_get_property(m_window->m_mpv, "duration", MPV_FORMAT_DOUBLE, &duration) < 0) duration = 0.0;
    position += milliseconds / 1000.0;
    position = qMax(0.0, position);
    if (duration > 0.0) position = qMin(duration, position);
    mpv_set_property(m_window->m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &position);
}

void PlayerPage::processMpvEvents()
{
    if (!m_window->m_mpv) return;
    while (true) {
        mpv_event *event = mpv_wait_event(m_window->m_mpv, 0);
        if (!event || event->event_id == MPV_EVENT_NONE) break;
        if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            auto *property = static_cast<mpv_event_property *>(event->data);
            if (!property || !property->name) continue;
            if (QString::fromUtf8(property->name) == "time-pos" && property->format == MPV_FORMAT_DOUBLE && property->data) {
                const double seconds = *static_cast<double *>(property->data);
                updatePlaybackPosition(qMax<qint64>(0, qRound64(seconds * 1000.0)));
            } else if (QString::fromUtf8(property->name) == "duration" && property->format == MPV_FORMAT_DOUBLE && property->data) {
                const double seconds = *static_cast<double *>(property->data);
                updateDuration(qMax<qint64>(0, qRound64(seconds * 1000.0)));
            }
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            auto *endFile = static_cast<mpv_event_end_file *>(event->data);
            if (!endFile || endFile->reason != MPV_END_FILE_REASON_EOF) continue;
            if (m_window->m_repeatMode == 2) {
                playTrack(m_window->m_currentTrack);
                continue;
            }
            const bool wasPlaylist = m_window->m_playbackFromPlaylist;
            const bool wasTracks = m_window->m_playbackFromTracks;
            const int oldQueueIndex = m_window->m_playbackQueueIndex;
            const int oldTrack = m_window->m_currentTrack;
            if (wasPlaylist || wasTracks) {
                if (oldQueueIndex + 1 < m_window->m_playbackQueue.size()) {
                    if (wasPlaylist) playPlaylistQueueTrack(oldQueueIndex + 1, m_window->m_currentPlaylist);
                    else playQueueTrack(oldQueueIndex + 1);
                } else if (m_window->m_repeatMode == 1) {
                    if (wasPlaylist) playPlaylistQueueTrack(0, m_window->m_currentPlaylist);
                    else playQueueTrack(0);
                } else {
                    m_window->m_trackEnded = true;
                    m_window->m_isPlaying = false;
                    m_window->m_playButton->setIcon(QIcon::fromTheme("media-playback-start"));
                    m_window->m_playButton->setToolTip("Play");
                }
            } else {
                const int next = m_window->m_albumPlaybackOrder.isEmpty()
                    ? oldTrack + 1
                    : m_window->m_albumPlaybackOrder.value(m_window->m_albumPlaybackOrder.indexOf(oldTrack) + 1, -1);
                if (next >= 0 && next < m_window->m_currentAlbum.tracks.size()) playTrack(next);
                else if (m_window->m_repeatMode == 1) playTrack(m_window->m_albumPlaybackOrder.isEmpty() ? 0 : m_window->m_albumPlaybackOrder.first());
                else {
                    m_window->m_trackEnded = true;
                    m_window->m_isPlaying = false;
                    m_window->m_playButton->setIcon(QIcon::fromTheme("media-playback-start"));
                    m_window->m_playButton->setToolTip("Play");
                }
            }
        }
    }
}

void PlayerPage::updatePlaybackPosition(qint64 position)
{
    if (!m_window->m_positionSlider->isSliderDown())
        m_window->m_positionSlider->setValue(static_cast<int>(position));
    
    m_window->m_currentTime->setText(formatTime(position));
    
    if (m_waveform)
        m_waveform->setPosition(position);
    
    updateLyrics(position);
}

void PlayerPage::updateDuration(qint64 duration)
{
    m_window->m_positionSlider->setRange(0, static_cast<int>(duration));
    
    if (m_waveform)
        m_waveform->setRange(duration);
    
    m_window->m_duration->setText(formatTime(duration));
}

bool PlayerPage::initializeMpv()
{
    setlocale(LC_NUMERIC, "C");
    m_window->m_mpv = mpv_create();
    if (!m_window->m_mpv) return false;
    const int optionResults[] = {
        mpv_set_option_string(m_window->m_mpv, "vo", "null"),
        mpv_set_option_string(m_window->m_mpv, "force-window", "no"),
        mpv_set_option_string(m_window->m_mpv, "terminal", "no"),
        mpv_set_option_string(m_window->m_mpv, "idle", "yes"),
        mpv_set_option_string(m_window->m_mpv, "video", "no"),
        mpv_set_option_string(m_window->m_mpv, "osc", "no"),
        mpv_set_option_string(m_window->m_mpv, "input-default-bindings", "no")
    };
    for (int result : optionResults) {
        if (result < 0) {
            const QString error = QString::fromUtf8(mpv_error_string(result));
            mpv_terminate_destroy(m_window->m_mpv);
            m_window->m_mpv = nullptr;
            QMessageBox::critical(m_window, "libmpv Error", "libmpv option setup failed:\n\n" + error);
            return false;
        }
    }
    const int result = mpv_initialize(m_window->m_mpv);
    if (result < 0) {
        const QString error = QString::fromUtf8(mpv_error_string(result));
        mpv_terminate_destroy(m_window->m_mpv);
        m_window->m_mpv = nullptr;
        QMessageBox::critical(m_window, "libmpv Error", "libmpv initialization failed:\n\n" + error);
        return false;
    }
    const int timeResult = mpv_observe_property(m_window->m_mpv, 1, "time-pos", MPV_FORMAT_DOUBLE);
    const int durationResult = mpv_observe_property(m_window->m_mpv, 2, "duration", MPV_FORMAT_DOUBLE);
    if (timeResult < 0 || durationResult < 0) {
        const int errorCode = timeResult < 0 ? timeResult : durationResult;
        const QString error = QString::fromUtf8(mpv_error_string(errorCode));
        mpv_terminate_destroy(m_window->m_mpv);
        m_window->m_mpv = nullptr;
        QMessageBox::critical(m_window, "libmpv Error", "Could not configure libmpv:\n\n" + error);
        return false;
    }
    return true;
}

void PlayerPage::updateLyricsFont()
{
    if (!m_window->m_lyrics) return;
    const int pointSize = qBound(14, m_window->width() / 70, 32);
    QFont font = m_window->m_lyrics->font();
    font.setPointSize(pointSize);
    m_window->m_lyrics->setFont(font);
    for (int i = 0; i < m_window->m_lyrics->count(); ++i) {
        QFont itemFont = font;
        itemFont.setBold(m_window->m_activeLyric >= 0 && i == m_window->m_activeLyric + 1);
        m_window->m_lyrics->item(i)->setFont(itemFont);
    }
    if (!m_window->m_lyricsData.isEmpty() && m_window->m_lyrics->count() >= 3) {
        const int spacerHeight = qMax(0, m_window->m_lyrics->viewport()->height() / 2 - m_window->m_lyrics->fontMetrics().height() / 2);
        m_window->m_lyrics->item(0)->setSizeHint(QSize(1, spacerHeight));
        m_window->m_lyrics->item(m_window->m_lyrics->count() - 1)->setSizeHint(QSize(1, spacerHeight));
        if (auto *widget = m_window->m_lyrics->itemWidget(m_window->m_lyrics->item(0))) widget->setFixedHeight(spacerHeight);
        if (auto *widget = m_window->m_lyrics->itemWidget(m_window->m_lyrics->item(m_window->m_lyrics->count() - 1))) widget->setFixedHeight(spacerHeight);
    }
}

void PlayerPage::populateLyrics()
{
    m_window->m_lyrics->clear();
    m_window->m_activeLyric = -1;
    if (m_window->m_lyricsData.isEmpty()) {
        auto *item = new QListWidgetItem("No synchronized lyrics found");
        item->setTextAlignment(Qt::AlignCenter);
        m_window->m_lyrics->addItem(item);
        return;
    }
    const int spacerHeight = qMax(0, m_window->m_lyrics->viewport()->height() / 2 - m_window->m_lyrics->fontMetrics().height() / 2);
    auto *topSpacer = new QListWidgetItem;
    topSpacer->setFlags(Qt::NoItemFlags);
    topSpacer->setSizeHint(QSize(1, spacerHeight));
    m_window->m_lyrics->addItem(topSpacer);
    auto *topSpaceWidget = new QWidget;
    topSpaceWidget->setFixedHeight(spacerHeight);
    m_window->m_lyrics->setItemWidget(topSpacer, topSpaceWidget);
    for (const LyricLine &line : m_window->m_lyricsData) {
        auto *item = new QListWidgetItem(line.text);
        item->setTextAlignment(Qt::AlignCenter);
        QFont itemFont = m_window->m_lyrics->font();
        itemFont.setBold(false);
        item->setFont(itemFont);
        m_window->m_lyrics->addItem(item);
    }
    auto *bottomSpacer = new QListWidgetItem;
    bottomSpacer->setFlags(Qt::NoItemFlags);
    bottomSpacer->setSizeHint(QSize(1, spacerHeight));
    m_window->m_lyrics->addItem(bottomSpacer);
    auto *bottomSpaceWidget = new QWidget;
    bottomSpaceWidget->setFixedHeight(spacerHeight);
    m_window->m_lyrics->setItemWidget(bottomSpacer, bottomSpaceWidget);
}

void PlayerPage::centerLyricItem(int itemIndex)
{
    if (!m_window->m_lyrics || itemIndex < 1 || itemIndex >= m_window->m_lyrics->count() - 1) return;
    m_window->m_lyrics->doItemsLayout();
    const QRect rect = m_window->m_lyrics->visualItemRect(m_window->m_lyrics->item(itemIndex));
    if (!rect.isValid()) return;
    QScrollBar *bar = m_window->m_lyrics->verticalScrollBar();
    const int target = bar->value() + rect.center().y() - m_window->m_lyrics->viewport()->height() / 2;
    bar->setValue(qBound(bar->minimum(), target, bar->maximum()));
}

void PlayerPage::updateLyrics(qint64 position)
{
    if (m_window->m_lyricsData.isEmpty())
        return;
    
    int active = -1;
    
    for (int i = 0; i < m_window->m_lyricsData.size(); ++i) {
        const qint64 lyricPosition = m_window->m_lyricsData[i].position;
        
        if (lyricPosition < 0)
            continue;
        
        if (lyricPosition <= position)
            active = i;
        else
            break;
    }
    
    if (active < 0 ||
        active >= m_window->m_lyricsData.size() ||
        active == m_window->m_activeLyric)
        return;
    
    const int itemIndex = active + 1;
    const QPalette palette = m_window->m_lyrics->palette();
    
    if (m_window->m_activeLyric >= 0 &&
        m_window->m_activeLyric < m_window->m_lyricsData.size()) {
        
        auto *item = m_window->m_lyrics->item(m_window->m_activeLyric + 1);
    QFont font = item->font();
    font.setBold(false);
    item->setFont(font);
    item->setForeground(palette.color(QPalette::Text));
        }
        
        auto *item = m_window->m_lyrics->item(itemIndex);
        QFont font = item->font();
        font.setBold(true);
        item->setFont(font);
        item->setForeground(palette.color(QPalette::Highlight));
        
        m_window->m_activeLyric = active;
        
        QTimer::singleShot(0, m_window, [this, itemIndex]() {
            centerLyricItem(itemIndex);
        });
}

QString PlayerPage::formatTime(qint64 milliseconds)
{
    const qint64 totalSeconds = qMax<qint64>(0, milliseconds) / 1000;
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    return QString::number(minutes) + ":" + QString("%1").arg(seconds, 2, 10, QLatin1Char('0'));
}
