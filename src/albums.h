#pragma once

#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSize>
#include <QWidget>

#include "scanning.h"

class MainWindow;

QIcon sangeetIcon();

class NavigationBar : public QWidget
{
    Q_OBJECT
public:
    enum class Page { Albums, Playlists, Tracks };

    explicit NavigationBar(QWidget *parent = nullptr);
    void setActive(Page page);

signals:
    void pageRequested(Page page);

private:
    QPushButton *createButton(const QString &text);
    QPushButton *m_albums = nullptr;
    QPushButton *m_playlists = nullptr;
    QPushButton *m_tracks = nullptr;
};

class AlbumCard : public QPushButton
{
    Q_OBJECT
public:
    explicit AlbumCard(const Album &album, QWidget *parent = nullptr);
    void setArtworkLoaded(bool loaded);
    void setCardWidth(int width);

signals:
    void albumClicked(const Album &album);

private:
    void updateArtwork();

    Album m_album;
    QLabel *m_art = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_artist = nullptr;
    QPixmap m_sourcePixmap;
    QSize m_loadedSize;
};

class Albums
{
public:
    explicit Albums(MainWindow *window);

    void createAlbumsPage();
    void filterAlbums();
    void scheduleGridRebuild();
    void clearGrid();
    QPair<int, int> gridSize() const;
    void addAlbumCard(int index, int columns, int cardWidth);
    void rebuildGrid();
    void scheduleArtworkUpdate();
    void updateVisibleArtwork();

private:
    MainWindow *m_window = nullptr;
};
