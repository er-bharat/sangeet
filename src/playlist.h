#pragma once

#include <QDialog>
#include <QList>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>

#include "scanning.h"

class MainWindow;
class QGridLayout;
class QLabel;
class QStackedWidget;
class QListView;
class QAbstractListModel;
class QTableView;
class QAbstractTableModel;

class PlaylistArtGrid : public QWidget
{
public:
    explicit PlaylistArtGrid(const QList<QString> &artworks = {}, QWidget *parent = nullptr);
    void setArtworks(const QList<QString> &artworks);
    void setSize(int size);

private:
    void rebuild();

    QList<QString> m_artworks;
    QGridLayout *m_grid = nullptr;
};

class PlaylistCard : public QPushButton
{
    Q_OBJECT
public:
    explicit PlaylistCard(const Playlist &playlist, const QList<QString> &artworks, QWidget *parent = nullptr);
    void setCardWidth(int width);

signals:
    void playlistClicked(const Playlist &playlist);
    void editRequested(const Playlist &playlist);

private:
    bool hasHttpStream() const;
    Playlist m_playlist;
    QList<QString> m_artworks;
    PlaylistArtGrid *m_artGrid = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_radioIcon = nullptr;
    QToolButton *m_editButton = nullptr;
};

class PlaylistEditorDialog : public QDialog
{
    Q_OBJECT
public:
    PlaylistEditorDialog(const Playlist &playlist, const QList<Album> &albums, QWidget *parent = nullptr);
    Playlist resultPlaylist() const;

private:
    void createEditorPage();
    void createAddPage();
    void setEditorButtons();
    void beginAddTracks();
    void finishAddTracks();

    Playlist m_playlist;
    Playlist m_result;
    const QList<Album> *m_albums = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_editorPage = nullptr;
    QWidget *m_addPage = nullptr;
    QListView *m_list = nullptr;
    QAbstractListModel *m_model = nullptr;
    QTableView *m_addTable = nullptr;
    QAbstractTableModel *m_addModel = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_doneButton = nullptr;
};

class Playlists
{
public:
    explicit Playlists(MainWindow *window);

    void createPlaylistsPage();
    void showPlaylistsPage();
    void rebuildPlaylistsGrid();
    void editPlaylist(const Playlist &playlist);

private:
    MainWindow *m_window = nullptr;
};
