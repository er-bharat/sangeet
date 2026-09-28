#include "scanning.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMap>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QRegularExpression>
#include <algorithm>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mp4item.h>

QString sortText(const QString &text)
{
    QString value = text.toCaseFolded();
    value.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), " ");
    return value.simplified();
}

bool isMusicFile(const QString &path)
{
	static const QSet<QString> extensions = {
		"mp3", "flac", "ogg", "oga", "opus", "wav",
		"m4a", "aac", "wma", "ape", "wv", "mpc"
	};
	
	return extensions.contains(
		QFileInfo(path).suffix().toLower()
	);
}

bool isPlaylistFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "m3u" || suffix == "m3u8";
}

static QString saveArtwork(const QByteArray &data, const QString &source)
{
	if (data.isEmpty())
		return {};
	
	QImage image;
	
	if (!image.loadFromData(data))
		return {};
	
	const QString cacheDirectory =
	QStandardPaths::writableLocation(
		QStandardPaths::CacheLocation
	) + "/Sangeet/album-art";
	
	if (!QDir().mkpath(cacheDirectory))
		return {};
	
	const QString filename =
	QString::number(qHash(source)) + ".jpg";
	
	const QString output =
	QDir(cacheDirectory).filePath(filename);
	
	if (!QFileInfo::exists(output))
		image.save(output, "JPEG", 95);
	
	return output;
}

static QByteArray mpegArtwork(TagLib::File *file)
{
    auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file);
    if (!mpeg) return {};
    auto *tag = mpeg->ID3v2Tag(false);
    if (!tag) return {};
    const auto map = tag->frameListMap();
    const auto it = map.find("APIC");
    if (it == map.end() || it->second.isEmpty()) return {};
    auto *picture = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(it->second.front());
    if (!picture) return {};
    const auto bytes = picture->picture();
    return QByteArray(bytes.data(), static_cast<int>(bytes.size()));
}

static QByteArray flacArtwork(TagLib::File *file)
{
    auto *flac = dynamic_cast<TagLib::FLAC::File *>(file);
    if (!flac || flac->pictureList().isEmpty()) return {};
    const auto bytes = flac->pictureList().front()->data();
    return QByteArray(bytes.data(), static_cast<int>(bytes.size()));
}

static QByteArray mp4Artwork(TagLib::File *file)
{
    auto *mp4 = dynamic_cast<TagLib::MP4::File *>(file);
    if (!mp4 || !mp4->tag()) return {};
    const auto items = mp4->tag()->itemMap();
    const auto it = items.find("covr");
    if (it == items.end()) return {};
    const auto covers = it->second.toCoverArtList();
    if (covers.isEmpty()) return {};
    const auto bytes = covers.front().data();
    return QByteArray(bytes.data(), static_cast<int>(bytes.size()));
}

static QString embeddedArtwork(const QString &path)
{
    TagLib::FileRef file(path.toUtf8().constData());
    if (file.isNull() || !file.file()) return {};

    QByteArray data;
    if (dynamic_cast<TagLib::MPEG::File *>(file.file()))
        data = mpegArtwork(file.file());
    else if (dynamic_cast<TagLib::FLAC::File *>(file.file()))
        data = flacArtwork(file.file());
    else if (dynamic_cast<TagLib::MP4::File *>(file.file()))
        data = mp4Artwork(file.file());

    return saveArtwork(data, path);
}

static QString externalArtwork(const QString &directory)
{
	const QStringList names = {
		"cover.jpg",
		"cover.jpeg",
		"cover.png",
		"cover.webp",
		"folder.jpg",
		"folder.jpeg",
		"folder.png",
		"folder.webp",
		"front.jpg",
		"front.jpeg",
		"front.png",
		"front.webp",
		"album.jpg",
		"album.jpeg",
		"album.png",
		"album.webp"
	};
	
	for (const QString &name : names) {
		const QString path =
		QDir(directory).filePath(name);
		
		if (QFileInfo::exists(path))
			return path;
	}
	
	QDir dir(directory);
	
	const QStringList images =
	dir.entryList(
		{
			"*.jpg",
			"*.jpeg",
			"*.png",
			"*.webp"
		},
		QDir::Files
	);
	
	if (!images.isEmpty())
		return dir.filePath(images.first());
	
	return {};
}

static QString findArtwork(const QString &musicFile)
{
	const QString directory =
	QFileInfo(musicFile).absolutePath();
	
	QString artwork =
	embeddedArtwork(musicFile);
	
	if (!artwork.isEmpty())
		return artwork;
	
	return externalArtwork(directory);
}

static QString tagText(const TagLib::String &value)
{
    return QString::fromStdString(value.to8Bit(true));
}

static void readTrackTags(Track &track, TagLib::FileRef &file)
{
    if (file.isNull() || !file.tag()) return;
    const auto *tag = file.tag();
    track.title = tagText(tag->title());
    track.artist = tagText(tag->artist());
    track.album = tagText(tag->album());
    track.genre = tagText(tag->genre());
    track.year = static_cast<int>(tag->year());
    track.trackNumber = static_cast<int>(tag->track());
}

static void readAlbumArtist(Track &track, TagLib::FileRef &file)
{
    if (file.isNull() || !file.file()) return;
    const auto properties = file.file()->properties();
    const auto it = properties.find("ALBUMARTIST");
    if (it != properties.end() && !it->second.isEmpty())
        track.albumArtist = tagText(it->second.front());
}

static void finishTrack(Track &track, const QString &path, TagLib::FileRef &file)
{
    if (track.title.isEmpty()) track.title = QFileInfo(path).completeBaseName();
    if (track.artist.isEmpty()) track.artist = "Unknown Artist";
    if (track.album.isEmpty()) track.album = "Unknown Album";
    track.fileType = QFileInfo(path).suffix().toUpper();
    if (file.audioProperties())
        track.frequency = file.audioProperties()->sampleRate();
    if (track.albumArtist.isEmpty()) track.albumArtist = track.artist;
}

static Track readTrack(const QString &path)
{
    Track track;
    track.path = path;
    TagLib::FileRef file(path.toUtf8().constData());
    readTrackTags(track, file);
    readAlbumArtist(track, file);
    finishTrack(track, path, file);
    return track;
}

static QString albumKey(const Track &track)
{
    return track.albumArtist.toLower() + "||" + track.album.toLower();
}

static void addTrack(QMap<QString, Album> &albums, const Track &track, const QString &artwork = {})
{
    const QString key = albumKey(track);
    if (!albums.contains(key))
        albums.insert(key, {track.album, track.albumArtist, artwork, {}});
    albums[key].tracks.append(track);
}

static void sortAlbums(QList<Album> &albums)
{
    for (Album &album : albums) {
        std::sort(album.tracks.begin(), album.tracks.end(), [](const Track &a, const Track &b) {
            return a.trackNumber != b.trackNumber
                ? a.trackNumber < b.trackNumber
                : sortText(a.title) < sortText(b.title);
        });
    }
    std::sort(albums.begin(), albums.end(), [](const Album &a, const Album &b) {
        return sortText(a.name) < sortText(b.name);
    });
}

static QList<Album> scanMusic(const QString &directory)
{
    QMap<QString, Album> albums;
    QDirIterator iterator(directory, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);

    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (isMusicFile(path)) {
            const Track track = readTrack(path);
            const QString key = albumKey(track);
            const QString artwork = albums.contains(key) ? QString() : findArtwork(path);
            addTrack(albums, track, artwork);
        }
    }

    QList<Album> result = albums.values();
    sortAlbums(result);
    return result;
}


static QMap<QString, QPair<int, int>> playlistTrackMap(const QList<Album> &albums)
{
    QMap<QString, QPair<int, int>> map;
    for (int ai = 0; ai < albums.size(); ++ai)
        for (int ti = 0; ti < albums[ai].tracks.size(); ++ti) {
            const QString path = QFileInfo(albums[ai].tracks[ti].path).absoluteFilePath();
            const QString key = QFileInfo(path).canonicalFilePath();
            map.insert(key.isEmpty() ? path : key, {ai, ti});
        }
    return map;
}

static Track streamTrack(const QString &url, const QString &title, const QString &playlistName)
{
    Track track;
    track.path = url;
    track.title = title.isEmpty() ? QUrl(url).fileName() : title;
    if (track.title.isEmpty()) track.title = url;
    track.fileType = "stream";
    track.album = playlistName;
    track.albumArtist = playlistName;
    return track;
}

static QString playlistEntry(QString line, const QString &playlistPath)
{
    if (line.startsWith("file://", Qt::CaseInsensitive))
        return QUrl(line).toLocalFile();
    if (QDir::isRelativePath(line))
        return QDir(QFileInfo(playlistPath).absolutePath()).filePath(line);
    return line;
}

static void addPlaylistEntry(Playlist &playlist, const QString &line,
                             const QString &playlistPath,
                             const QString &pendingTitle,
                             const QMap<QString, QPair<int, int>> &trackMap,
                             const QList<Album> &albums)
{
    if (line.startsWith("http://", Qt::CaseInsensitive) ||
        line.startsWith("https://", Qt::CaseInsensitive)) {
        playlist.refs.append({-1, playlist.tracks.size()});
        playlist.tracks.append(streamTrack(line, pendingTitle, playlist.name));
        return;
    }

    const QFileInfo info(playlistEntry(line, playlistPath));
    if (!info.exists() || !info.isFile() || !isMusicFile(info.absoluteFilePath()))
        return;

    QString key = info.canonicalFilePath();
    if (key.isEmpty()) key = info.absoluteFilePath();
    const auto it = trackMap.constFind(key);
    if (it == trackMap.constEnd()) return;

    playlist.refs.append(it.value());
    playlist.tracks.append(albums[it.value().first].tracks[it.value().second]);
}

static Playlist readPlaylist(const QString &path, const QMap<QString, QPair<int, int>> &trackMap,
                             const QList<Album> &albums)
{
    Playlist playlist;
    playlist.name = QFileInfo(path).completeBaseName();
    playlist.path = path;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return playlist;

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    QString pendingTitle;

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.startsWith(QChar(0xFEFF))) line.remove(0, 1);
        if (line.isEmpty()) continue;

        if (line.startsWith("#EXTINF:", Qt::CaseInsensitive)) {
            const qsizetype comma = line.indexOf(',');
            pendingTitle = comma < 0 ? QString() : line.mid(comma + 1).trimmed();
            continue;
        }
        if (line.startsWith('#')) continue;

        addPlaylistEntry(playlist, line, path, pendingTitle, trackMap, albums);
        pendingTitle.clear();
    }
    return playlist;
}

static QList<Playlist> scanPlaylists(const QString &directory, const QList<Album> &albums)
{
    const auto trackMap = playlistTrackMap(albums);
    QList<Playlist> result;
    QDirIterator iterator(directory, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);

    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (!isPlaylistFile(path)) continue;
        Playlist playlist = readPlaylist(path, trackMap, albums);
        if (!playlist.tracks.isEmpty())
            result.append(playlist);
    }

    std::sort(result.begin(), result.end(), [](const Playlist &a, const Playlist &b) {
        return sortText(a.name) < sortText(b.name);
    });
    return result;
}


// =========================================================
// Application services
// =========================================================



struct CachedTrack {
    Track track;
    qint64 mtime = 0;
    qint64 size = 0;
};

class Scanning::Impl
{
public:
    bool loadMusic(const QString &directory)
    {
        const QString root = QFileInfo(directory).canonicalFilePath().isEmpty()
            ? QFileInfo(directory).absoluteFilePath()
            : QFileInfo(directory).canonicalFilePath();

        m_albums = loadFromDatabase(root);
        m_playlists = scanPlaylists(root, m_albums);

        return !m_albums.isEmpty() || !m_playlists.isEmpty();
    }

    const QList<Album> &albums() const { return m_albums; }
    const QList<Playlist> &playlists() const { return m_playlists; }

private:
    static QString databasePath()
    {
        const QString directory =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(directory);
        return QDir(directory).filePath(QStringLiteral("Sangeet.db"));
    }

    static bool ensureSchema(QSqlDatabase &db)
    {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tracks ("
            "path TEXT PRIMARY KEY, "
            "root TEXT NOT NULL, "
            "mtime INTEGER NOT NULL, "
            "size INTEGER NOT NULL, "
            "title TEXT, "
            "artist TEXT, "
            "album TEXT, "
            "album_artist TEXT, "
            "file_type TEXT, "
            "frequency INTEGER, "
            "genre TEXT, "
            "track_number INTEGER, "
            "year INTEGER, "
            "artwork TEXT)")))
            return false;

        if (!q.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS tracks_root ON tracks(root)")))
            return false;

        return q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS album_art ("
            "root TEXT NOT NULL, "
            "album_key TEXT NOT NULL, "
            "artwork TEXT, "
            "PRIMARY KEY(root, album_key))"));
    }

    static bool writeTrack(QSqlDatabase &db, const QString &root, const Track &track)
    {
        const QFileInfo info(track.path);
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO tracks "
            "(path, root, mtime, size, title, artist, album, album_artist, "
            "file_type, frequency, genre, track_number, year, artwork) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        q.addBindValue(info.absoluteFilePath());
        q.addBindValue(root);
        q.addBindValue(info.lastModified().toSecsSinceEpoch());
        q.addBindValue(info.size());
        q.addBindValue(track.title);
        q.addBindValue(track.artist);
        q.addBindValue(track.album);
        q.addBindValue(track.albumArtist);
        q.addBindValue(track.fileType);
        q.addBindValue(track.frequency);
        q.addBindValue(track.genre);
        q.addBindValue(track.trackNumber);
        q.addBindValue(track.year);
        return q.exec();
    }

    static QMap<QString, CachedTrack> cachedTracks(QSqlDatabase &db, const QString &root)
    {
        QMap<QString, CachedTrack> cached;
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "SELECT path, mtime, size, title, artist, album, album_artist, file_type, "
            "frequency, genre, track_number, year FROM tracks WHERE root = ?"));
        query.addBindValue(root);
        if (!query.exec()) return cached;

        while (query.next()) {
            CachedTrack entry;
            entry.mtime = query.value(1).toLongLong();
            entry.size = query.value(2).toLongLong();

            Track track;
            track.path = query.value(0).toString();
            track.title = query.value(3).toString();
            track.artist = query.value(4).toString();
            track.album = query.value(5).toString();
            track.albumArtist = query.value(6).toString();
            track.fileType = query.value(7).toString();
            track.frequency = query.value(8).toInt();
            track.genre = query.value(9).toString();
            track.trackNumber = query.value(10).toInt();
            track.year = query.value(11).toInt();

            entry.track = track;
            cached.insert(track.path, entry);
        }
        return cached;
    }

    static QMap<QString, QString> cachedAlbumArt(QSqlDatabase &db, const QString &root)
    {
        QMap<QString, QString> cached;
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "SELECT album_key, artwork FROM album_art WHERE root = ?"));
        query.addBindValue(root);
        if (!query.exec())
            return cached;

        while (query.next())
            cached.insert(query.value(0).toString(), query.value(1).toString());
        return cached;
    }

    static void saveAlbumArt(QSqlDatabase &db, const QString &root,
                             const QString &key, const QString &artwork)
    {
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO album_art (root, album_key, artwork) "
            "VALUES (?, ?, ?)"));
        query.addBindValue(root);
        query.addBindValue(key);
        query.addBindValue(artwork);
        query.exec();
    }

    static void appendDatabaseTrack(QMap<QString, Album> &albums, const Track &track,
                                    const QString &artwork)
    {
        addTrack(albums, track, artwork);
    }

    static void removeMissingTracks(QSqlDatabase &db, const QString &root,
                                    const QSet<QString> &currentPaths)
    {
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT path FROM tracks WHERE root = ?"));
        query.addBindValue(root);
        if (!query.exec()) return;

        QSqlQuery remove(db);
        remove.prepare(QStringLiteral("DELETE FROM tracks WHERE path = ? AND root = ?"));
        while (query.next()) {
            const QString path = query.value(0).toString();
            if (currentPaths.contains(path)) continue;
            remove.bindValue(0, path);
            remove.bindValue(1, root);
            remove.exec();
        }
    }

    QList<Album> loadFromDatabase(const QString &root)
    {
        const QString connectionName = QStringLiteral("sangeet_library");
        if (QSqlDatabase::contains(connectionName))
            QSqlDatabase::removeDatabase(connectionName);

        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath());

        if (!db.open() || !ensureSchema(db)) {
            if (db.isOpen()) db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            return scanMusic(root);
        }

        const auto cached = cachedTracks(db, root);
        const auto cachedArt = cachedAlbumArt(db, root);
        QSet<QString> currentPaths;
        QMap<QString, Album> albums;
        QDirIterator iterator(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);

        db.transaction();
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            if (!isMusicFile(path))
                continue;

            const QFileInfo info(path);
            const QString absolutePath = info.absoluteFilePath();
            currentPaths.insert(absolutePath);

            const auto cachedIt = cached.constFind(absolutePath);
            Track track;

            if (cachedIt != cached.constEnd() &&
                cachedIt.value().mtime == info.lastModified().toSecsSinceEpoch() &&
                cachedIt.value().size == info.size()) {
                track = cachedIt.value().track;
            } else {
                track = readTrack(absolutePath);
                writeTrack(db, root, track);
            }

            const QString key = albumKey(track);
            QString artwork = cachedArt.value(key);
            if (artwork.isEmpty() || !QFileInfo::exists(artwork)) {
                artwork = findArtwork(absolutePath);
                saveAlbumArt(db, root, key, artwork);
            }

            appendDatabaseTrack(albums, track, artwork);
        }

        removeMissingTracks(db, root, currentPaths);
        db.commit();

        QList<Album> result = albums.values();
        sortAlbums(result);
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        return result;
    }

    QList<Album> m_albums;
    QList<Playlist> m_playlists;
};

Scanning::Scanning() : m_impl(new Impl)
{
}

Scanning::~Scanning()
{
    delete m_impl;
}

bool Scanning::loadMusic(const QString &directory)
{
    return m_impl->loadMusic(directory);
}

const QList<Album> &Scanning::albums() const
{
    return m_impl->albums();
}

const QList<Playlist> &Scanning::playlists() const
{
    return m_impl->playlists();
}
