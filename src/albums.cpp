#include "albums.h"
#include "mainwindow.h"
#include "folder_select.h"

#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QScrollBar>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

QIcon sangeetIcon()
{
    return QIcon(QStringLiteral(":/sangeet.svg"));
}

NavigationBar::NavigationBar(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    auto *brand = new QLabel;
    brand->setPixmap(sangeetIcon().pixmap(32, 32));
    brand->setFixedSize(40, 40);
    brand->setAlignment(Qt::AlignCenter);
    brand->setToolTip("Sangeet");
    layout->addWidget(brand);
    
    m_albums = createButton("Albums");
    m_playlists = createButton("Playlists");
    m_tracks = createButton("Tracks");
    
    layout->addWidget(m_albums);
    layout->addWidget(m_playlists);
    layout->addWidget(m_tracks);
    
    connect(m_albums, &QPushButton::clicked, this, [this]() {
        emit pageRequested(Page::Albums);
    });
    
    connect(m_playlists, &QPushButton::clicked, this, [this]() {
        emit pageRequested(Page::Playlists);
    });
    
    connect(m_tracks, &QPushButton::clicked, this, [this]() {
        emit pageRequested(Page::Tracks);
    });
    
    setActive(Page::Albums);
}

void NavigationBar::setActive(Page page)
{
    QFont normal = font();
    normal.setPointSize(22);
    normal.setWeight(QFont::Normal);
    
    QFont bold = normal;
    bold.setWeight(QFont::Bold);
    
    m_albums->setFont(page == Page::Albums ? bold : normal);
    m_playlists->setFont(page == Page::Playlists ? bold : normal);
    m_tracks->setFont(page == Page::Tracks ? bold : normal);
}

QPushButton *NavigationBar::createButton(const QString &text)
{
    auto *button = new QPushButton(text);
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(
        "QPushButton { background: transparent; border: none; padding: 4px 8px; color: palette(text); }"
        "QPushButton:hover { color: #4da3ff; }"
    );
    return button;
}

AlbumCard::AlbumCard(const Album &album, QWidget *parent)
    : QPushButton(parent), m_album(album)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    m_art = new QLabel;
    m_art->setAlignment(Qt::AlignCenter);
    m_art->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(m_art, 0, Qt::AlignHCenter);

    m_title = new QLabel(album.name);
    m_title->setAlignment(Qt::AlignCenter);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_title->setWordWrap(false);
    m_title->setFixedHeight(m_title->fontMetrics().lineSpacing());
    layout->addWidget(m_title);

    m_artist = new QLabel(album.artist);
    m_artist->setAlignment(Qt::AlignCenter);
    m_artist->setWordWrap(false);
    m_artist->setFixedHeight(m_artist->fontMetrics().lineSpacing());
    layout->addWidget(m_artist);

    QObject::connect(this, &QPushButton::clicked, this, [this]() {
        emit albumClicked(m_album);
    });
}

void AlbumCard::setArtworkLoaded(bool loaded)
{
    if (!loaded) {
        m_sourcePixmap = QPixmap();
        m_art->clear();
        return;
    }

    if (!m_album.artwork.isEmpty()) {
        const QSize target = m_art->size();
        if (target.width() > 0 && target.height() > 0 &&
            (m_sourcePixmap.isNull() || m_loadedSize != target)) {
            QImageReader reader(m_album.artwork);
            reader.setAutoTransform(true);

            const QSize source = reader.size();
            QSize scaled = target;
            if (source.isValid() && source.width() > 0 && source.height() > 0) {
                const qreal sx = qreal(target.width()) / source.width();
                const qreal sy = qreal(target.height()) / source.height();
                const qreal scale = qMax(sx, sy);
                scaled = QSize(qMax(1, qRound(source.width() * scale)),
                               qMax(1, qRound(source.height() * scale)));
            }

            reader.setScaledSize(scaled);
            const QImage image = reader.read();
            if (!image.isNull()) {
                m_sourcePixmap = QPixmap::fromImage(image);
                m_loadedSize = target;
            }
        }
    }

    updateArtwork();
}

void AlbumCard::setCardWidth(int width)
{
    const int padding = 16;
    const int artSize = width - padding;
    const int textWidth = qMax(1, width - padding);

    setFixedWidth(width);
    m_art->setFixedSize(artSize, artSize);
    updateArtwork();

    const QFontMetrics titleMetrics(m_title->font());
    const QFontMetrics artistMetrics(m_artist->font());

    auto limitText = [](const QString &text, const QFontMetrics &metrics, int availableWidth) {
        if (metrics.horizontalAdvance(text) <= availableWidth)
            return text;

        const QString ellipsis = QString::fromUtf8("…");
        int chars = 0;
        while (chars < text.size() &&
               metrics.horizontalAdvance(text.left(chars + 1) + ellipsis) <= availableWidth)
            ++chars;

        return chars > 0 ? text.left(chars) + ellipsis : ellipsis;
    };

    m_title->setText(limitText(m_album.name, titleMetrics, textWidth));
    m_artist->setText(limitText(m_album.artist, artistMetrics, textWidth));

    const int titleHeight = titleMetrics.lineSpacing();
    const int artistHeight = artistMetrics.lineSpacing();
    m_title->setFixedHeight(titleHeight);
    m_artist->setFixedHeight(artistHeight);
    setFixedHeight(artSize + titleHeight + artistHeight + 32);
}

void AlbumCard::updateArtwork()
{
    if (m_sourcePixmap.isNull()) {
        m_art->clear();
        return;
    }

    const QSize size = m_art->size();
    if (size.width() <= 0 || size.height() <= 0)
        return;

    m_art->setPixmap(m_sourcePixmap.scaled(
        size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
}

Albums::Albums(MainWindow *window) : m_window(window)
{
}

void Albums::createAlbumsPage()
{
    m_window->m_albumsPage = new QWidget;
    auto *mainLayout = new QVBoxLayout(m_window->m_albumsPage);
    mainLayout->setContentsMargins(15, 15, 15, 15);

    auto *topLayout = new QHBoxLayout;
    auto *navigation = new NavigationBar;
    m_window->m_albumsNavigation = navigation;
    topLayout->addWidget(navigation);

    m_window->m_search = new QLineEdit;
    m_window->m_search->setPlaceholderText("Search albums or music...");
    topLayout->addStretch();
    topLayout->addWidget(m_window->m_search);
    m_window->m_searchButton = m_window->m_folderSelect->createSearchButton(m_window->m_search);
    topLayout->addWidget(m_window->m_searchButton);

    auto *settingsButton = m_window->m_folderSelect->createSettings();
    topLayout->addWidget(settingsButton);
    mainLayout->addLayout(topLayout);

    m_window->m_scrollArea = new QScrollArea;
    m_window->m_scrollArea->setWidgetResizable(true);
    m_window->m_albumsContainer = new QWidget;
    m_window->m_grid = new QGridLayout(m_window->m_albumsContainer);
    m_window->m_grid->setSpacing(20);
    m_window->m_grid->setContentsMargins(20, 20, 20, 20);
    m_window->m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_window->m_scrollArea->setWidget(m_window->m_albumsContainer);
    mainLayout->addWidget(m_window->m_scrollArea, 1);

    m_window->m_artworkTimer = new QTimer(m_window);
    m_window->m_artworkTimer->setSingleShot(true);
    m_window->m_artworkTimer->setInterval(30);
    QObject::connect(m_window->m_artworkTimer, &QTimer::timeout, m_window, [this]() {
        m_window->m_albumsController->updateVisibleArtwork();
    });

    QObject::connect(m_window->m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
                     m_window, [this]() { m_window->m_albumsController->scheduleArtworkUpdate(); });
    QObject::connect(m_window->m_scrollArea->horizontalScrollBar(), &QScrollBar::valueChanged,
                     m_window, [this]() { m_window->m_albumsController->scheduleArtworkUpdate(); });
    QObject::connect(m_window->m_search, &QLineEdit::textChanged,
                     m_window, [this]() { m_window->m_albumsController->filterAlbums(); });

    QObject::connect(navigation, &NavigationBar::pageRequested, m_window, [this](NavigationBar::Page page) {
        if (page == NavigationBar::Page::Albums) {
            emit m_window->tracksNavigationRequested(false);
            m_window->m_stack->setCurrentWidget(m_window->m_albumsPage);
            emit m_window->albumsGridRebuildRequested();
        } else if (page == NavigationBar::Page::Playlists) {
            emit m_window->showPlaylistsRequested();
        } else {
            emit m_window->showTracksRequested();
        }
    });
}

void Albums::filterAlbums()
{
    const QString query = m_window->m_search->text().trimmed().toLower();
    m_window->m_filteredAlbums.clear();

    if (query.isEmpty()) {
        m_window->m_filteredAlbums = m_window->m_albums;
    } else {
        for (const Album &album : m_window->m_albums) {
            bool matches = album.name.toLower().contains(query) ||
                           album.artist.toLower().contains(query);
            if (!matches) {
                for (const Track &track : album.tracks) {
                    if (track.title.toLower().contains(query) ||
                        track.artist.toLower().contains(query)) {
                        matches = true;
                        break;
                    }
                }
            }
            if (matches)
                m_window->m_filteredAlbums.append(album);
        }
    }

    if (m_window->m_stack->currentWidget() == m_window->m_albumsPage)
        emit m_window->albumsGridRebuildRequested();
    if (m_window->m_lyrics)
        emit m_window->lyricsFontUpdateRequested();
}

void Albums::scheduleGridRebuild()
{
    if (!m_window->m_gridRebuildTimer) {
        m_window->m_gridRebuildTimer = new QTimer(m_window);
        m_window->m_gridRebuildTimer->setSingleShot(true);
        QObject::connect(m_window->m_gridRebuildTimer, &QTimer::timeout, m_window, [this]() {
            if (m_window->m_stack->currentWidget() == m_window->m_albumsPage)
                m_window->m_albumsController->rebuildGrid();
        });
    }
    m_window->m_gridRebuildTimer->start(0);
}

void Albums::clearGrid()
{
    while (QLayoutItem *item = m_window->m_grid->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}

QPair<int, int> Albums::gridSize() const
{
    const int width = m_window->m_scrollArea->viewport()->width();
    const auto margins = m_window->m_grid->contentsMargins();
    const int spacing = m_window->m_grid->horizontalSpacing();
    const int usable = qMax(1, width - margins.left() - margins.right());
    const int columns = qMax(1, (usable + spacing) / (190 + spacing));
    const int cardWidth = qMax(190, (usable - spacing * (columns - 1)) / columns);
    return {columns, cardWidth};
}

void Albums::addAlbumCard(int index, int columns, int cardWidth)
{
    auto *card = new AlbumCard(m_window->m_filteredAlbums[index], m_window->m_albumsPage);
    card->setCardWidth(cardWidth);
    QObject::connect(card, &AlbumCard::albumClicked, m_window,
                     [this](const Album &album) { emit m_window->openAlbumRequested(album); });
    m_window->m_grid->addWidget(card, index / columns, index % columns,
                                Qt::AlignTop | Qt::AlignLeft);
    if (index == 0)
        card->setFocus();
}

void Albums::rebuildGrid()
{
    clearGrid();
    const auto [columns, cardWidth] = gridSize();
    for (int i = 0; i < m_window->m_filteredAlbums.size(); ++i)
        addAlbumCard(i, columns, cardWidth);
    scheduleArtworkUpdate();
}

void Albums::scheduleArtworkUpdate()
{
    if (m_window->m_artworkTimer)
        m_window->m_artworkTimer->start();
}

void Albums::updateVisibleArtwork()
{
    if (!m_window->m_scrollArea || !m_window->m_scrollArea->viewport())
        return;

    const QRect viewportRect = m_window->m_scrollArea->viewport()->rect();
    const auto cards = m_window->m_albumsContainer->findChildren<AlbumCard *>();
    for (AlbumCard *card : cards) {
        const QRect cardRect(
            card->mapTo(m_window->m_scrollArea->viewport(), QPoint(0, 0)),
            card->size());
        const bool visible = viewportRect.intersects(cardRect.adjusted(0, -300, 0, 300));
        card->setArtworkLoaded(visible);
    }
}
