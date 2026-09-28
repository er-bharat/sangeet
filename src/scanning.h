#pragma once

#include <QList>
#include <QPair>
#include <QString>

struct Track {
    QString path;
    QString title;
    QString artist;
    QString album;
    QString albumArtist;
    QString fileType;
    int frequency = 0;
    QString genre;
    int trackNumber = 0;
    int year = 0;
};

struct Album {
    QString name;
    QString artist;
    QString artwork;
    QList<Track> tracks;
};

struct Playlist {
    QString name;
    QString path;
    QList<Track> tracks;
    QList<QPair<int, int>> refs;
};

struct TrackRef {
    int albumIndex = -1;
    int trackIndex = -1;
};

QString sortText(const QString &text);
bool isMusicFile(const QString &path);
bool isPlaylistFile(const QString &path);

class Scanning
{
public:
    Scanning();
    ~Scanning();

    bool loadMusic(const QString &directory);
    const QList<Album> &albums() const;
    const QList<Playlist> &playlists() const;

private:
    class Impl;
    Impl *m_impl = nullptr;
};
