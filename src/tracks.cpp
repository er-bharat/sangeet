#include "tracks.h"
#include "mainwindow.h"
#include "folder_select.h"

#include <QApplication>
#include <QFile>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTableView>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <algorithm>

Tracks::Tracks(MainWindow *window) : m_window(window) {}

void Tracks::createTracksPage()
{
    m_window->m_tracksPage = new QWidget;
    auto *layout = new QVBoxLayout(m_window->m_tracksPage);
    layout->setContentsMargins(15, 15, 15, 15);

    auto *top = new QHBoxLayout;
    auto *navigation = new NavigationBar;
    m_window->m_tracksNavigation = navigation;
    top->addWidget(navigation);

    m_window->m_tracksSearch = new QLineEdit;
    m_window->m_tracksSearch->setPlaceholderText("Search tracks...");
    top->addStretch();
    top->addWidget(m_window->m_tracksSearch);
    m_window->m_tracksSearchButton = m_window->m_folderSelect->createSearchButton(m_window->m_tracksSearch);
    top->addWidget(m_window->m_tracksSearchButton);

    m_window->m_createPlaylistButton = new QPushButton("+ Playlist");
    top->addWidget(m_window->m_createPlaylistButton);

    auto *settingsButton = m_window->m_folderSelect->createSettings();
    top->addWidget(settingsButton);
    layout->addLayout(top);

    m_window->m_tracksTable = new QTableView;
    m_window->m_tracksModel = new TrackTableModel(&m_window->m_albums, m_window);
    m_window->m_tracksTable->setModel(m_window->m_tracksModel);
    m_window->m_tracksTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_window->m_tracksTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_window->m_tracksTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_window->m_tracksTable->setFocusPolicy(Qt::StrongFocus);
    auto *verticalHeader = m_window->m_tracksTable->verticalHeader();
    verticalHeader->setVisible(true);
    verticalHeader->setMinimumWidth(40);
    auto *header = m_window->m_tracksTable->horizontalHeader();
    header->setStretchLastSection(false);
    for (int column = 0; column < 7; ++column)
        header->setSectionResizeMode(column, QHeaderView::Interactive);
    header->setSortIndicatorShown(true);
    header->setSectionsClickable(true);
    layout->addWidget(m_window->m_tracksTable, 1);

    m_window->m_playlistBar = new QWidget;
    auto *playlistBarLayout = new QHBoxLayout(m_window->m_playlistBar);
    playlistBarLayout->setContentsMargins(0, 0, 0, 0);
    playlistBarLayout->addStretch();
    m_window->m_playlistCancelButton = new QPushButton("Cancel");
    m_window->m_playlistCreateButton = new QPushButton("Create");
    playlistBarLayout->addWidget(m_window->m_playlistCancelButton);
    playlistBarLayout->addWidget(m_window->m_playlistCreateButton);
    m_window->m_playlistBar->setVisible(false);
    layout->addWidget(m_window->m_playlistBar);

    QObject::connect(m_window->m_playlistCancelButton, &QPushButton::clicked, m_window, [this]() {
        m_window->m_tracksController->cancelPlaylistCreation();
    });
    QObject::connect(m_window->m_playlistCreateButton, &QPushButton::clicked, m_window, [this]() {
        m_window->m_tracksController->createPlaylistFile();
    });

    QObject::connect(header, &QHeaderView::sectionClicked, m_window, [this](int section) {
        if (m_window->m_creatingPlaylist || section <= 1)
            return;
        const int sortColumn = section - 1;
        if (sortColumn == m_window->m_tracksSortColumn)
            m_window->m_tracksSortAscending = !m_window->m_tracksSortAscending;
        else {
            m_window->m_tracksSortColumn = sortColumn;
            m_window->m_tracksSortAscending = true;
        }
        emit m_window->showTracksRequested(m_window->m_tracksAlbumFilter);
    });

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

    QObject::connect(m_window->m_createPlaylistButton, &QPushButton::clicked, m_window, [this]() {
        m_window->m_tracksController->beginPlaylistCreation();
    });

    QObject::connect(m_window->m_tracksSearch, &QLineEdit::textChanged, m_window, [this]() {
        if (!m_window->m_creatingPlaylist)
            emit m_window->showTracksRequested(m_window->m_tracksAlbumFilter);
    });

    QObject::connect(m_window->m_tracksTable, &QTableView::doubleClicked, m_window, [this](const QModelIndex &index) {
        const int row = index.row();
        if (row < 0 || row >= m_window->m_trackRows.size())
            return;
        m_window->m_playbackQueue = m_window->m_trackRows;
        m_window->m_sequentialPlaybackQueue = m_window->m_playbackQueue;
        m_window->m_playbackQueueIndex = row;
        m_window->m_playbackFromTracks = true;
        const TrackRef &ref = m_window->m_playbackQueue[row];
        emit m_window->openAlbumRequested(m_window->m_albums[ref.albumIndex], ref.trackIndex, true);
    });
}

void Tracks::setNavigationPage(bool tracksPage, bool playlistsPage)
{
    const NavigationBar::Page page =
        tracksPage ? NavigationBar::Page::Tracks :
        playlistsPage ? NavigationBar::Page::Playlists : NavigationBar::Page::Albums;
    if (m_window->m_albumsNavigation)
        m_window->m_albumsNavigation->setActive(page);
    if (m_window->m_playlistsNavigation)
        m_window->m_playlistsNavigation->setActive(page);
    if (m_window->m_tracksNavigation)
        m_window->m_tracksNavigation->setActive(page);
}

void Tracks::resizeTracksColumnsToContents()
{
    if (!m_window->m_tracksTable || !m_window->m_tracksModel)
        return;

    auto *header = m_window->m_tracksTable->horizontalHeader();
    const QFontMetrics metrics(m_window->m_tracksTable->font());
    const int padding = 28;
    const int count = m_window->m_tracksModel->rowCount();
    QList<int> widths(7, 0);
    for (int column = 0; column < widths.size(); ++column)
        widths[column] = metrics.horizontalAdvance(m_window->m_tracksModel->headerData(column, Qt::Horizontal).toString()) + padding;

    for (int row = 0; row < count; ++row) {
        for (int column = 1; column < widths.size(); ++column) {
            const QString text = m_window->m_tracksModel->data(m_window->m_tracksModel->index(row, column), Qt::DisplayRole).toString();
            widths[column] = qMax(widths[column], metrics.horizontalAdvance(text) + padding);
        }
    }

    widths[0] = 40;
    widths[1] = qBound(56, widths[1], 80);
    widths[2] = qBound(180, widths[2], 500);
    widths[3] = qBound(120, widths[3], 280);
    widths[4] = qBound(120, widths[4], 280);
    widths[5] = qBound(80, widths[5], 120);
    widths[6] = qBound(90, widths[6], 130);

    const int viewportWidth = m_window->m_tracksTable->viewport()->width();
    const bool checkboxVisible = !m_window->m_tracksTable->isColumnHidden(0);
    int fixedWidth = checkboxVisible ? widths[0] : 0;
    for (int column = 1; column < widths.size(); ++column)
        fixedWidth += widths[column];

    if (viewportWidth > fixedWidth)
        widths[2] += viewportWidth - fixedWidth;
    else if (viewportWidth > 0) {
        int excess = fixedWidth - viewportWidth;
        for (const int column : {4, 3, 5, 6}) {
            const int minimum = column == 5 ? 70 : column == 6 ? 80 : 100;
            const int reduction = qMin(excess, qMax(0, widths[column] - minimum));
            widths[column] -= reduction;
            excess -= reduction;
            if (excess == 0)
                break;
        }
        if (excess > 0)
            widths[2] = qMax(120, widths[2] - excess);
    }

    for (int column = 0; column < widths.size(); ++column)
        header->resizeSection(column, widths[column]);
}

void Tracks::showTracksPage(const QString &albumFilter)
{
    m_window->m_tracksController->setNavigationPage(true, false);
    if (!m_window->m_tracksPage || !m_window->m_tracksTable)
        return;

    m_window->m_tracksAlbumFilter = albumFilter;
    m_window->m_trackRows.clear();

    const QString query = m_window->m_tracksSearch ? m_window->m_tracksSearch->text().trimmed().toLower() : QString();
    QList<TrackRef> rows;
    for (int albumIndex = 0; albumIndex < m_window->m_albums.size(); ++albumIndex) {
        const Album &album = m_window->m_albums[albumIndex];
        if (!albumFilter.isEmpty() && album.name != albumFilter)
            continue;
        for (int trackIndex = 0; trackIndex < album.tracks.size(); ++trackIndex) {
            const Track &track = album.tracks[trackIndex];
            if (!query.isEmpty()) {
                const QString haystack = track.title + " " + track.artist + " " + track.album;
                if (!haystack.toLower().contains(query))
                    continue;
            }
            rows.append({albumIndex, trackIndex});
        }
    }

    std::stable_sort(rows.begin(), rows.end(), [this](const TrackRef &a, const TrackRef &b) {
        const Track &ta = m_window->m_albums[a.albumIndex].tracks[a.trackIndex];
        const Track &tb = m_window->m_albums[b.albumIndex].tracks[b.trackIndex];
        const Album &aa = m_window->m_albums[a.albumIndex];
        const Album &ab = m_window->m_albums[b.albumIndex];
        int result = 0;

        switch (m_window->m_tracksSortColumn) {
        case 1:
            result = QString::compare(sortText(ta.title), sortText(tb.title), Qt::CaseInsensitive);
            break;
        case 2:
            result = QString::compare(sortText(ta.artist), sortText(tb.artist), Qt::CaseInsensitive);
            if (result == 0)
                result = QString::compare(sortText(ta.title), sortText(tb.title), Qt::CaseInsensitive);
            break;
        case 3:
            result = QString::compare(sortText(ta.album), sortText(tb.album), Qt::CaseInsensitive);
            if (result == 0)
                result = ta.trackNumber - tb.trackNumber;
            if (result == 0)
                result = QString::compare(sortText(ta.title), sortText(tb.title), Qt::CaseInsensitive);
            break;
        case 4:
            result = QString::compare(sortText(ta.fileType), sortText(tb.fileType), Qt::CaseInsensitive);
            if (result == 0)
                result = QString::compare(sortText(ta.title), sortText(tb.title), Qt::CaseInsensitive);
            break;
        case 5:
            result = (ta.frequency > tb.frequency) - (ta.frequency < tb.frequency);
            if (result == 0)
                result = QString::compare(sortText(ta.title), sortText(tb.title), Qt::CaseInsensitive);
            break;
        default:
            result = QString::compare(sortText(ta.title), sortText(tb.title), Qt::CaseInsensitive);
            break;
        }

        if (result == 0)
            result = QString::compare(sortText(aa.name), sortText(ab.name), Qt::CaseInsensitive);
        if (result == 0)
            result = a.trackIndex - b.trackIndex;
        return m_window->m_tracksSortAscending ? result < 0 : result > 0;
    });

    m_window->m_trackRows = rows;
    m_window->m_tracksModel->setRows(rows);
    m_window->m_tracksTable->horizontalHeader()->setSortIndicator(
        m_window->m_tracksSortColumn + 1,
        m_window->m_tracksSortAscending ? Qt::AscendingOrder : Qt::DescendingOrder);
    m_window->m_tracksTable->setColumnHidden(0, !m_window->m_creatingPlaylist);
    m_window->m_stack->setCurrentWidget(m_window->m_tracksPage);

    if (!m_window->m_tracksColumnsInitialized) {
        QTimer::singleShot(0, m_window, [this]() {
            resizeTracksColumnsToContents();
            m_window->m_tracksColumnsInitialized = true;
        });
    }
}

void Tracks::beginPlaylistCreation()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        m_window, "Create Playlist", "Playlist name:", QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    QString safeName = name;
    safeName.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), "_");
    safeName = safeName.trimmed();
    if (safeName.isEmpty())
        return;

    m_window->m_playlistCreationName = safeName;
    m_window->m_creatingPlaylist = true;
    m_window->m_tracksSearch->setEnabled(false);
    m_window->m_createPlaylistButton->setEnabled(false);
    m_window->m_tracksTable->setColumnHidden(0, false);
    m_window->m_playlistBar->setVisible(true);
    emit m_window->showTracksRequested(m_window->m_tracksAlbumFilter);
    m_window->m_tracksTable->setColumnHidden(0, false);
}

void Tracks::cancelPlaylistCreation()
{
    m_window->m_creatingPlaylist = false;
    m_window->m_playlistCreationName.clear();
    m_window->m_tracksSearch->setEnabled(true);
    m_window->m_createPlaylistButton->setEnabled(true);
    m_window->m_playlistBar->setVisible(false);
    emit m_window->showTracksRequested(m_window->m_tracksAlbumFilter);
}

void Tracks::createPlaylistFile()
{
    const QList<TrackRef> selected = m_window->m_tracksModel->checkedRows();
    if (selected.isEmpty()) {
        QMessageBox::information(m_window, "Create Playlist", "Select at least one track.");
        return;
    }

    const QString playlistDirectory = QDir(m_window->m_musicBaseDirectory).filePath("playlist");
    if (!QDir().mkpath(playlistDirectory)) {
        QMessageBox::critical(m_window, "Create Playlist", "Could not create the playlist directory.");
        return;
    }

    const QString path = QDir(playlistDirectory).filePath(m_window->m_playlistCreationName + ".m3u");
    if (QFileInfo::exists(path)) {
        const auto answer = QMessageBox::question(
            m_window, "Create Playlist", "A playlist with m_window name already exists. Replace it?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(m_window, "Create Playlist", "Could not create:\n" + path);
        return;
    }

    QTextStream out(&file);
    out << "#EXTM3U\n";
    for (const TrackRef &ref : selected) {
        if (ref.albumIndex >= 0 && ref.albumIndex < m_window->m_albums.size() &&
            ref.trackIndex >= 0 && ref.trackIndex < m_window->m_albums[ref.albumIndex].tracks.size())
            out << QDir::toNativeSeparators(m_window->m_albums[ref.albumIndex].tracks[ref.trackIndex].path) << "\n";
    }
    file.close();

    QMessageBox::information(m_window, "Playlist Created", "Playlist created:\n" + path);

    m_window->m_scanning->loadMusic(m_window->m_musicBaseDirectory);

    m_window->m_tracksController->cancelPlaylistCreation();
    emit m_window->showPlaylistsRequested();
}
