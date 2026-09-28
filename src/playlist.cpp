#include "playlist.h"
#include "mainwindow.h"
#include "folder_select.h"
#include "albums.h"
#include "tracks.h"

#include <QApplication>
#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QListView>
#include <QMessageBox>
#include <QPainter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableView>
#include <QTextStream>
#include <QVBoxLayout>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QMap>
#include <QSet>
#include <QFile>
#include <QDir>
#include <QTimer>

#include <algorithm>

PlaylistArtGrid::PlaylistArtGrid(const QList<QString> &artworks, QWidget *parent)
    : QWidget(parent), m_artworks(artworks)
{
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(0);
}

void PlaylistArtGrid::setArtworks(const QList<QString> &artworks)
{
    m_artworks = artworks;
    rebuild();
}

void PlaylistArtGrid::setSize(int size)
{
    setFixedSize(size, size);
    rebuild();
}

void PlaylistArtGrid::rebuild()
{
    if (!m_grid)
        return;

    while (QLayoutItem *item = m_grid->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    const int side = qMax(1, width() / 2);
    for (int i = 0; i < m_artworks.size() && i < 4; ++i) {
        auto *label = new QLabel;
        label->setFixedSize(side, side);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet("background: #222;");

        QImageReader reader(m_artworks[i]);
        reader.setAutoTransform(true);
        reader.setScaledSize(QSize(side, side));
        const QImage image = reader.read();
        if (!image.isNull())
            label->setPixmap(QPixmap::fromImage(image).scaled(
                label->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));

        m_grid->addWidget(label, i / 2, i % 2);
    }
}

PlaylistCard::PlaylistCard(const Playlist &playlist, const QList<QString> &artworks, QWidget *parent)
    : QPushButton(parent), m_playlist(playlist), m_artworks(artworks)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    m_artGrid = new PlaylistArtGrid(artworks);
    layout->addWidget(m_artGrid, 0, Qt::AlignHCenter);

    m_radioIcon = new QLabel(m_artGrid);
    m_radioIcon->setAlignment(Qt::AlignCenter);
    m_radioIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_radioIcon->setStyleSheet("background: rgba(0,0,0,150); border-radius: 6px;");
    QIcon icon = QIcon::fromTheme(QStringLiteral("radio"));
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("audio-x-generic"));
    if (!icon.isNull())
        m_radioIcon->setPixmap(icon.pixmap(QSize(28, 28)));
    m_radioIcon->setVisible(hasHttpStream());

    m_title = new QLabel(playlist.name);
    m_title->setAlignment(Qt::AlignCenter);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_title->setWordWrap(false);
    layout->addWidget(m_title);

    m_subtitle = new QLabel;
    m_subtitle->setFixedHeight(m_subtitle->fontMetrics().lineSpacing());
    layout->addWidget(m_subtitle);

    m_editButton = new QToolButton(this);
    const QIcon editIcon = QIcon::fromTheme(
        QStringLiteral("document-edit"), QIcon::fromTheme(QStringLiteral("edit")));
    if (!editIcon.isNull())
        m_editButton->setIcon(editIcon);
    else
        m_editButton->setText(QStringLiteral("✎"));
    m_editButton->setIconSize(QSize(18, 18));
    m_editButton->setAutoRaise(true);
    m_editButton->setCursor(Qt::PointingHandCursor);
    m_editButton->setToolTip(QStringLiteral("Edit playlist"));
    m_editButton->raise();

    connect(m_editButton, &QToolButton::clicked, this, [this]() {
        emit editRequested(m_playlist);
    });
    connect(this, &QPushButton::clicked, this, [this]() {
        emit playlistClicked(m_playlist);
    });
}

void PlaylistCard::setCardWidth(int width)
{
    const int padding = 16;
    const int artSize = width - padding;
    const QFontMetrics metrics(m_title->font());
    const int titleHeight = metrics.lineSpacing();
    const int subtitleHeight = m_subtitle->fontMetrics().lineSpacing();

    setFixedWidth(width);
    m_artGrid->setSize(artSize);
    m_radioIcon->setGeometry(artSize - 42, 10, 36, 36);
    setFixedHeight(artSize + titleHeight + subtitleHeight + 32);

    const QString text = m_playlist.name;
    if (metrics.horizontalAdvance(text) <= artSize) {
        m_title->setText(text);
    } else {
        const QString ellipsis = QString::fromUtf8("…");
        int chars = 0;
        while (chars < text.size() &&
               metrics.horizontalAdvance(text.left(chars + 1) + ellipsis) <= artSize)
            ++chars;
        m_title->setText(chars > 0 ? text.left(chars) + ellipsis : ellipsis);
    }

    m_title->setFixedHeight(titleHeight);
    m_editButton->setGeometry(width - 38, height() - 38, 30, 30);
}

bool PlaylistCard::hasHttpStream() const
{
    for (const Track &track : m_playlist.tracks) {
        if (track.path.startsWith("http://", Qt::CaseInsensitive) ||
            track.path.startsWith("https://", Qt::CaseInsensitive))
            return true;
    }
    return false;
}

class PlaylistTrackModel : public QAbstractTableModel
{
public:
    explicit PlaylistTrackModel(const QList<Album> *albums, QObject *parent = nullptr)
        : QAbstractTableModel(parent), m_albums(albums) {}

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }

    int columnCount(const QModelIndex &parent = {}) const override
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
            QStringLiteral("Artist"), QStringLiteral("Album"), QStringLiteral("Filetype"),
            QStringLiteral("Frequency")};
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

    QList<TrackRef> checkedRows() const
    {
        QList<int> checked = m_checked.values();
        std::sort(checked.begin(), checked.end());
        QList<TrackRef> result;
        for (const int row : checked)
            if (row >= 0 && row < m_rows.size())
                result.append(m_rows[row]);
        return result;
    }

    void clearChecks()
    {
        if (m_checked.isEmpty())
            return;
        const QSet<int> checked = m_checked;
        m_checked.clear();
        for (int row : checked)
            if (row >= 0 && row < m_rows.size())
                emit dataChanged(index(row, 0), index(row, 0), {Qt::CheckStateRole});
    }

private:
    const QList<Album> *m_albums = nullptr;
    QList<TrackRef> m_rows;
    QSet<int> m_checked;
};

class PlaylistEditorModel : public QAbstractListModel
{
public:
    PlaylistEditorModel(Playlist *playlist, QObject *parent = nullptr)
        : QAbstractListModel(parent), m_playlist(playlist) {}

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_playlist->tracks.size());
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_playlist->tracks.size())
            return {};
        const Track &track = m_playlist->tracks[index.row()];
        if (role == Qt::DisplayRole)
            return track.title;
        if (role == Qt::ToolTipRole)
            return track.artist;
        return {};
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return Qt::ItemIsDropEnabled;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    }

    QStringList mimeTypes() const override
    {
        return {QStringLiteral("application/x-sangeet-playlist-row")};
    }

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        auto *mime = new QMimeData;
        if (!indexes.isEmpty())
            mime->setData(mimeTypes().first(), QByteArray::number(indexes.first().row()));
        return mime;
    }

    Qt::DropActions supportedDropActions() const override
    {
        return Qt::MoveAction;
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int, const QModelIndex &parent) override
    {
        if (action != Qt::MoveAction || !data || !data->hasFormat(mimeTypes().first()))
            return false;
        bool ok = false;
        const int source = QString::fromUtf8(data->data(mimeTypes().first())).toInt(&ok);
        if (!ok || source < 0 || source >= m_playlist->tracks.size())
            return false;
        int destination = row;
        if (destination < 0)
            destination = parent.isValid() ? parent.row() : rowCount();
        destination = qBound(0, destination, rowCount());
        if (destination == source || destination == source + 1)
            return false;
        if (source < destination)
            --destination;
        return moveRows({}, source, 1, {}, destination);
    }

    bool moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                  const QModelIndex &destinationParent, int destinationChild) override
    {
        if (sourceParent.isValid() || destinationParent.isValid() || count != 1 ||
            sourceRow < 0 || sourceRow >= rowCount() || destinationChild < 0 ||
            destinationChild > rowCount() || destinationChild == sourceRow ||
            destinationChild == sourceRow + 1)
            return false;
        if (!beginMoveRows(sourceParent, sourceRow, sourceRow, destinationParent, destinationChild))
            return false;
        const int target = destinationChild > sourceRow ? destinationChild - 1 : destinationChild;
        m_playlist->tracks.move(sourceRow, target);
        m_playlist->refs.move(sourceRow, target);
        endMoveRows();
        return true;
    }

    void refresh()
    {
        beginResetModel();
        endResetModel();
    }

    bool removeRow(int row)
    {
        if (row < 0 || row >= rowCount())
            return false;
        beginRemoveRows({}, row, row);
        m_playlist->tracks.removeAt(row);
        m_playlist->refs.removeAt(row);
        endRemoveRows();
        return true;
    }

private:
    Playlist *m_playlist = nullptr;
};

class PlaylistEditorDelegate : public QStyledItemDelegate
{
public:
    explicit PlaylistEditorDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    static QRect handleRect(const QRect &rect)
    {
        return QRect(rect.left() + 8, rect.top() + 2, 32, rect.height() - 4);
    }

    static QRect removeRect(const QRect &rect)
    {
        return QRect(rect.right() - 40, rect.top() + 2, 32, rect.height() - 4);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.text.clear();
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        const QRect r = option.rect;
        painter->save();
        painter->setPen(option.palette.color(QPalette::Text));
        QFont handleFont = painter->font();
        handleFont.setPointSize(handleFont.pointSize() + 2);
        painter->setFont(handleFont);
        painter->drawText(handleRect(r), Qt::AlignCenter, QStringLiteral("☰"));
        painter->setFont(option.font);
        painter->drawText(QRect(r.left() + 48, r.top(), r.width() - 96, r.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, index.data(Qt::DisplayRole).toString());
        const QIcon cutIcon = QIcon::fromTheme(
            QStringLiteral("edit-cut"), QIcon::fromTheme(QStringLiteral("edit-delete")));
        if (!cutIcon.isNull())
            cutIcon.paint(painter, removeRect(r), Qt::AlignCenter);
        else
            painter->drawText(removeRect(r), Qt::AlignCenter, QStringLiteral("✂"));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(0, 36);
    }
};

class PlaylistEditorView : public QListView
{
public:
    explicit PlaylistEditorView(QWidget *parent = nullptr) : QListView(parent)
    {
        setSelectionMode(QAbstractItemView::SingleSelection);
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::InternalMove);
        setDefaultDropAction(Qt::MoveAction);
        setMouseTracking(true);
        setUniformItemSizes(true);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        const QModelIndex index = indexAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && index.isValid()) {
            const QRect rect = visualRect(index);
            if (PlaylistEditorDelegate::removeRect(rect).contains(event->position().toPoint())) {
                if (auto *model = static_cast<PlaylistEditorModel *>(this->model()))
                    model->removeRow(index.row());
                event->accept();
                return;
            }
            if (PlaylistEditorDelegate::handleRect(rect).contains(event->position().toPoint())) {
                m_dragStartPosition = event->position().toPoint();
                m_dragIndex = index;
                setCurrentIndex(index);
                event->accept();
                return;
            }
        }
        m_dragIndex = {};
        QListView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragIndex.isValid() &&
            (event->position().toPoint() - m_dragStartPosition).manhattanLength() >= QApplication::startDragDistance()) {
            const QModelIndex index = m_dragIndex;
            m_dragIndex = {};
            setCurrentIndex(index);
            const QRect rect = visualRect(index);
            auto *drag = new QDrag(this);
            drag->setMimeData(model()->mimeData({index}));
            QPixmap pixmap(rect.size());
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            QStyleOptionViewItem option;
            initViewItemOption(&option);
            option.rect = QRect(QPoint(0, 0), rect.size());
            itemDelegate()->paint(&painter, option, index);
            painter.end();
            drag->setPixmap(pixmap);
            drag->setHotSpot(m_dragStartPosition - rect.topLeft());
            drag->exec(Qt::MoveAction);
            event->accept();
            return;
        }
        QListView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_dragIndex.isValid() && event->button() == Qt::LeftButton) {
            m_dragIndex = {};
            event->accept();
            return;
        }
        QListView::mouseReleaseEvent(event);
    }

private:
    QPoint m_dragStartPosition;
    QModelIndex m_dragIndex;
};

PlaylistEditorDialog::PlaylistEditorDialog(const Playlist &playlist, const QList<Album> &albums, QWidget *parent)
    : QDialog(parent), m_playlist(playlist), m_albums(&albums)
{
    setWindowTitle(QStringLiteral("Edit Playlist — ") + playlist.name);
    resize(800, 600);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(15, 15, 15, 15);
    root->setSpacing(10);
    m_stack = new QStackedWidget;
    root->addWidget(m_stack, 1);

    createEditorPage();
    createAddPage();
    m_stack->setCurrentWidget(m_editorPage);

    auto *bar = new QHBoxLayout;
    bar->addStretch();
    m_cancelButton = new QPushButton(QStringLiteral("Cancel"));
    m_addButton = new QPushButton(QStringLiteral("Add"));
    m_doneButton = new QPushButton(QStringLiteral("Done"));
    bar->addWidget(m_cancelButton);
    bar->addWidget(m_addButton);
    bar->addWidget(m_doneButton);
    root->addLayout(bar);

    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        if (m_stack->currentWidget() == m_addPage) {
            static_cast<PlaylistTrackModel *>(m_addModel)->clearChecks();
            m_stack->setCurrentWidget(m_editorPage);
            setEditorButtons();
        } else {
            reject();
        }
    });
    connect(m_addButton, &QPushButton::clicked, this, [this]() { beginAddTracks(); });
    connect(m_doneButton, &QPushButton::clicked, this, [this]() {
        if (m_stack->currentWidget() == m_addPage) {
            finishAddTracks();
            return;
        }
        m_result = m_playlist;
        accept();
    });
}

Playlist PlaylistEditorDialog::resultPlaylist() const
{
    return m_result;
}

void PlaylistEditorDialog::createEditorPage()
{
    m_editorPage = new QWidget;
    auto *layout = new QVBoxLayout(m_editorPage);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel(m_playlist.name);
    QFont font = title->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 2);
    title->setFont(font);
    layout->addWidget(title);

    auto *list = new PlaylistEditorView;
    m_list = list;
    auto *model = new PlaylistEditorModel(&m_playlist, list);
    m_model = model;
    list->setModel(model);
    list->setItemDelegate(new PlaylistEditorDelegate(list));
    layout->addWidget(list, 1);
    m_stack->addWidget(m_editorPage);
}

void PlaylistEditorDialog::createAddPage()
{
    m_addPage = new QWidget;
    auto *layout = new QVBoxLayout(m_addPage);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel(QStringLiteral("Add tracks"));
    QFont font = title->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 2);
    title->setFont(font);
    layout->addWidget(title);

    m_addTable = new QTableView;
    auto *model = new PlaylistTrackModel(m_albums, m_addTable);
    m_addModel = model;

    QList<TrackRef> rows;
    for (int ai = 0; ai < m_albums->size(); ++ai)
        for (int ti = 0; ti < (*m_albums)[ai].tracks.size(); ++ti)
            rows.append({ai, ti});
    model->setRows(rows);

    m_addTable->setModel(model);
    m_addTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_addTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_addTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_addTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_addTable->horizontalHeader()->resizeSection(0, 40);
    for (int column = 1; column < model->columnCount(); ++column)
        m_addTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    layout->addWidget(m_addTable, 1);
    m_stack->addWidget(m_addPage);
}

void PlaylistEditorDialog::setEditorButtons()
{
    m_addButton->setVisible(true);
    m_doneButton->setText(QStringLiteral("Done"));
}

void PlaylistEditorDialog::beginAddTracks()
{
    static_cast<PlaylistTrackModel *>(m_addModel)->clearChecks();
    m_stack->setCurrentWidget(m_addPage);
    m_addButton->setVisible(false);
    m_doneButton->setText(QStringLiteral("Done"));
}

void PlaylistEditorDialog::finishAddTracks()
{
    auto *model = static_cast<PlaylistTrackModel *>(m_addModel);
    for (const TrackRef &ref : model->checkedRows()) {
        if (ref.albumIndex < 0 || ref.albumIndex >= m_albums->size())
            continue;
        const Album &album = (*m_albums)[ref.albumIndex];
        if (ref.trackIndex < 0 || ref.trackIndex >= album.tracks.size())
            continue;
        m_playlist.tracks.append(album.tracks[ref.trackIndex]);
        m_playlist.refs.append({ref.albumIndex, ref.trackIndex});
    }
    model->clearChecks();
    static_cast<PlaylistEditorModel *>(m_model)->refresh();
    m_stack->setCurrentWidget(m_editorPage);
    setEditorButtons();
}

Playlists::Playlists(MainWindow *window)
    : m_window(window)
{
}

void Playlists::createPlaylistsPage()
{
    m_window->m_playlistsPage = new QWidget;
    auto *mainLayout = new QVBoxLayout(m_window->m_playlistsPage);
    mainLayout->setContentsMargins(15, 15, 15, 15);

    auto *topLayout = new QHBoxLayout;
    auto *navigation = new NavigationBar;
    m_window->m_playlistsNavigation = navigation;
    topLayout->addWidget(navigation);

    m_window->m_playlistsSearch = new QLineEdit;
    m_window->m_playlistsSearch->setPlaceholderText("Search playlists...");
    topLayout->addStretch();
    topLayout->addWidget(m_window->m_playlistsSearch);
    m_window->m_playlistsSearchButton = m_window->m_folderSelect->createSearchButton(m_window->m_playlistsSearch);
    topLayout->addWidget(m_window->m_playlistsSearchButton);
    topLayout->addWidget(m_window->m_folderSelect->createHomeButton());
    mainLayout->addLayout(topLayout);

    m_window->m_playlistsScroll = new QScrollArea;
    m_window->m_playlistsScroll->setWidgetResizable(true);
    m_window->m_playlistsContainer = new QWidget;
    m_window->m_playlistsGrid = new QGridLayout(m_window->m_playlistsContainer);
    m_window->m_playlistsGrid->setSpacing(20);
    m_window->m_playlistsGrid->setContentsMargins(20, 20, 20, 20);
    m_window->m_playlistsGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_window->m_playlistsScroll->setWidget(m_window->m_playlistsContainer);
    mainLayout->addWidget(m_window->m_playlistsScroll, 1);

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

    QObject::connect(m_window->m_playlistsSearch, &QLineEdit::textChanged, m_window, [this]() {
        emit m_window->playlistsRebuildRequested();
    });
}

void Playlists::showPlaylistsPage()
{
    emit m_window->tracksNavigationRequested(false, true);
    m_window->m_stack->setCurrentWidget(m_window->m_playlistsPage);
    QTimer::singleShot(0, m_window, [this]() {
        if (m_window->m_stack->currentWidget() == m_window->m_playlistsPage)
            emit m_window->playlistsRebuildRequested();
    });
}

void Playlists::rebuildPlaylistsGrid()
{
    if (!m_window->m_playlistsGrid)
        return;

    while (QLayoutItem *item = m_window->m_playlistsGrid->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    const QString query = m_window->m_playlistsSearch
        ? sortText(m_window->m_playlistsSearch->text().trimmed()) : QString();
    QList<Playlist> filtered;
    for (const Playlist &playlist : m_window->m_playlists) {
        if (query.isEmpty() || sortText(playlist.name).contains(query))
            filtered.append(playlist);
    }

    const int availableWidth = m_window->m_playlistsScroll->viewport()->width();
    const QMargins margins = m_window->m_playlistsGrid->contentsMargins();
    const int spacing = m_window->m_playlistsGrid->horizontalSpacing();
    const int minimumCardWidth = 190;
    const int usableWidth = qMax(1, availableWidth - margins.left() - margins.right());
    const int columns = qMax(1, (usableWidth + spacing) / (minimumCardWidth + spacing));
    const int cardWidth = qMax(minimumCardWidth,
        (usableWidth - spacing * (columns - 1)) / columns);

    QMap<QString, QString> artworkByAlbum;
    for (const Album &album : m_window->m_albums) {
        const QString key = album.artist.toCaseFolded() + QStringLiteral("||") + album.name.toCaseFolded();
        if (!album.artwork.isEmpty())
            artworkByAlbum.insert(key, album.artwork);
    }

    for (int i = 0; i < filtered.size(); ++i) {
        QList<QString> artworks;
        QSet<QString> albumsSeen;
        for (const Track &track : filtered[i].tracks) {
            const QString key = track.albumArtist.toCaseFolded() + QStringLiteral("||") + track.album.toCaseFolded();
            if (albumsSeen.contains(key))
                continue;
            albumsSeen.insert(key);
            const QString art = artworkByAlbum.value(key);
            if (!art.isEmpty())
                artworks.append(art);
            if (artworks.size() == 4)
                break;
        }

        auto *card = new PlaylistCard(filtered[i], artworks, m_window->m_playlistsPage);
        card->setCardWidth(cardWidth);
        QObject::connect(card, &PlaylistCard::playlistClicked, m_window,
            [this](const Playlist &playlist) { emit m_window->openPlaylistRequested(playlist, 0); });
        QObject::connect(card, &PlaylistCard::editRequested, m_window,
            [this](const Playlist &playlist) { editPlaylist(playlist); });
        m_window->m_playlistsGrid->addWidget(card, i / columns, i % columns,
                                             Qt::AlignTop | Qt::AlignLeft);
        if (i == 0)
            card->setFocus();
    }
}

void Playlists::editPlaylist(const Playlist &playlist)
{
    PlaylistEditorDialog dialog(playlist, m_window->m_albums, m_window);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const Playlist edited = dialog.resultPlaylist();
    QFile file(edited.path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(m_window, QStringLiteral("Edit Playlist"),
                              QStringLiteral("Could not write:\n") + edited.path);
        return;
    }

    QTextStream out(&file);
    out << "#EXTM3U\n";
    for (const Track &track : edited.tracks)
        out << QDir::toNativeSeparators(track.path) << "\n";
    file.close();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_window->m_scanning->loadMusic(m_window->m_musicBaseDirectory);
    m_window->m_albums = m_window->m_scanning->albums();
    m_window->m_playlists = m_window->m_scanning->playlists();
    m_window->m_filteredAlbums = m_window->m_albums;
    QApplication::restoreOverrideCursor();
    emit m_window->showPlaylistsRequested();
}
