// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lyricdownloader.h"
#include "lyricapi_ne.h"
#include "lyricapi_kg.h"

#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QThread>
#include <QDebug>

#include "util/log.h"

LyricDownloader::LyricDownloader(QObject *parent)
    : QObject(parent)
    , m_netManager(new QNetworkAccessManager(this))
    , m_neApi(new NetEaseApi(this))
    , m_kgApi(new KugouApi(this))
{
}

LyricDownloader::~LyricDownloader() = default;

QString LyricDownloader::downloadAndSaveLyrics(const DMusic::MediaMeta &meta,
                                               const QString &savePath,
                                               QString *savedPath)
{
    if (meta.localPath.isEmpty()) {
        qCWarning(dmMusic) << "Media meta localPath is empty, cannot download lyrics";
        return QString();
    }

    // Try sources in order: Kugou -> NetEase -> LRCLIB (Kugou has better word-by-word lyrics)
    QString lyricsText;

    qCInfo(dmMusic) << "Trying Kugou for:" << meta.title;
    lyricsText = fetchFromKugou(meta);
    if (!lyricsText.isEmpty()) {
        qCInfo(dmMusic) << "Got lyrics from Kugou, size:" << lyricsText.size();
    }

    if (lyricsText.isEmpty()) {
        qCInfo(dmMusic) << "Trying NetEase for:" << meta.title;
        lyricsText = fetchFromNetEase(meta);
        if (!lyricsText.isEmpty()) {
            qCInfo(dmMusic) << "Got lyrics from NetEase, size:" << lyricsText.size();
        }
    }

    if (lyricsText.isEmpty()) {
        qCInfo(dmMusic) << "Trying LRCLIB for:" << meta.title;
        lyricsText = fetchFromLrclib(meta);
        if (!lyricsText.isEmpty()) {
            qCInfo(dmMusic) << "Got lyrics from LRCLIB";
        }
    }

    if (lyricsText.isEmpty()) {
        qCWarning(dmMusic) << "No lyrics found from any source for:" << meta.title;
        return QString();
    }

    // Save to primary path
    qCInfo(dmMusic) << "Saving lyrics to:" << savePath << "size:" << lyricsText.size();
    QFile lrcFile(savePath);
    bool saved = false;
    if (lrcFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&lrcFile);
        stream << lyricsText;
        lrcFile.close();
        saved = true;
        qCInfo(dmMusic) << "Saved lyrics to:" << savePath;
    } else {
        qCWarning(dmMusic) << "Failed to save lyrics to:" << savePath << "- trying cache fallback";
    }

    // Fallback to cache directory
    if (!saved) {
        QString cacheDir = DmGlobal::cachePath() + "/lyrics";
        QDir().mkpath(cacheDir);
        QFileInfo fi(meta.localPath);
        QString fallbackPath = cacheDir + "/" + fi.completeBaseName() + ".lrc";
        QFile fallbackFile(fallbackPath);
        if (fallbackFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&fallbackFile);
            stream << lyricsText;
            fallbackFile.close();
            saved = true;
            qCInfo(dmMusic) << "Saved lyrics to cache fallback:" << fallbackPath;
            if (savedPath) *savedPath = fallbackPath;
        }
    }

    if (saved && savedPath && savedPath->isEmpty())
        *savedPath = savePath;

    return saved ? lyricsText : QString();
}

QString LyricDownloader::fetchFromNetEase(const DMusic::MediaMeta &meta)
{
    try {
        return m_neApi->searchAndGetLyrics(meta);
    } catch (...) {
        qCWarning(dmMusic) << "NetEase API exception";
        return QString();
    }
}

QString LyricDownloader::fetchFromKugou(const DMusic::MediaMeta &meta)
{
    try {
        return m_kgApi->searchAndGetLyrics(meta);
    } catch (...) {
        qCWarning(dmMusic) << "Kugou API exception";
        return QString();
    }
}

QString LyricDownloader::fetchFromLrclib(const DMusic::MediaMeta &meta)
{
    return fetchLyricsFromLrclib(meta);
}

// LRCLIB implementation (kept from original)
QString LyricDownloader::fetchLyricsFromLrclib(const DMusic::MediaMeta &meta)
{
    QString title = meta.title;
    QString artist = meta.artist;
    QString keyword = !artist.isEmpty() ? QString("%1 - %2").arg(artist, title) : title;

    QUrl searchUrl("https://lrclib.net/api/search");
    QUrlQuery query;
    query.addQueryItem("q", keyword);
    searchUrl.setQuery(query);

    QByteArray searchResponse = httpGet(searchUrl.toString());
    if (searchResponse.isEmpty()) return QString();

    QJsonDocument searchDoc = QJsonDocument::fromJson(searchResponse);
    if (!searchDoc.isArray()) return QString();

    QJsonArray searchResults = searchDoc.array();
    if (searchResults.isEmpty()) return QString();

    int songIndex = -1;
    for (int i = 0; i < searchResults.size(); i++) {
        QJsonObject obj = searchResults[i].toObject();
        QString trackName = obj.value("trackName").toString();
        QString artistName = obj.value("artistName").toString();
        bool titleMatch = trackName.compare(title, Qt::CaseInsensitive) == 0
                          || trackName.contains(title, Qt::CaseInsensitive);
        bool artistMatch = artist.isEmpty()
                           || artistName.compare(artist, Qt::CaseInsensitive) == 0
                           || artistName.contains(artist, Qt::CaseInsensitive);
        if (titleMatch && artistMatch) { songIndex = i; break; }
    }
    if (songIndex == -1) {
        for (int i = 0; i < searchResults.size(); i++) {
            QJsonObject obj = searchResults[i].toObject();
            if (obj.value("trackName").toString().contains(title, Qt::CaseInsensitive)) {
                songIndex = i; break;
            }
        }
    }
    if (songIndex == -1) songIndex = 0;

    QJsonObject songObj = searchResults[songIndex].toObject();
    QUrl lyricUrl("https://lrclib.net/api/get");
    QUrlQuery lyricQuery;
    lyricQuery.addQueryItem("track_name", songObj.value("trackName").toString());
    lyricQuery.addQueryItem("artist_name", songObj.value("artistName").toString());
    lyricQuery.addQueryItem("album_name", songObj.value("albumName").toString());
    lyricQuery.addQueryItem("duration", QString::number(songObj.value("duration").toDouble(), 'f', 1));
    lyricUrl.setQuery(lyricQuery);

    QByteArray lyricResponse = httpGet(lyricUrl.toString());
    if (lyricResponse.isEmpty()) return QString();

    QJsonDocument lyricDoc = QJsonDocument::fromJson(lyricResponse);
    if (!lyricDoc.isObject()) return QString();

    QJsonObject lyricObj = lyricDoc.object();
    if (lyricObj.contains("error")) return QString();

    QString lyricsText;
    if (lyricObj.contains("syncedLyrics") && !lyricObj.value("syncedLyrics").toString().isEmpty())
        lyricsText = lyricObj.value("syncedLyrics").toString();
    else if (lyricObj.contains("plainLyrics") && !lyricObj.value("plainLyrics").toString().isEmpty())
        lyricsText = lyricObj.value("plainLyrics").toString();

    return lyricsText;
}

QByteArray LyricDownloader::httpGet(const QString &url)
{
    const int maxRetries = 3;
    const int timeoutMs = 15000;

    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        if (attempt > 0) QThread::msleep(1000 * attempt);

        QNetworkRequest request = QNetworkRequest(QUrl(url));
        request.setHeader(QNetworkRequest::UserAgentHeader, "deepin-music/1.0");

        QEventLoop loop;
        QNetworkReply *reply = m_netManager->get(request);
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(timeoutMs);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray result = reply->readAll();
            reply->deleteLater();
            return result;
        }

        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 429) {
            qCWarning(dmMusic) << "Rate limited (429), retry" << (attempt + 1);
            reply->deleteLater();
            continue;
        }

        reply->deleteLater();
        return QByteArray();
    }
    return QByteArray();
}

QList<LyricSearchResult> LyricDownloader::searchLyrics(const QString &keyword)
{
    QList<LyricSearchResult> results;

    // Search Kugou first (has better word-by-word lyrics)
    try {
        QList<KgsearchResult> kgResults = m_kgApi->searchSongs(keyword);
        for (const auto &r : kgResults) {
            LyricSearchResult item;
            item.title = r.title;
            item.artist = r.artist;
            item.album = r.album;
            item.source = "Kugou";
            item.id = r.id;
            item.duration = r.duration;
            item.kugouHash = r.hash;
            results.append(item);
        }
    } catch (...) {
        qCWarning(dmMusic) << "Kugou search exception";
    }

    // Search NetEase
    try {
        QList<NesearchResult> neResults = m_neApi->searchSongs(keyword);
        for (const auto &r : neResults) {
            LyricSearchResult item;
            item.title = r.title;
            item.artist = r.artist;
            item.album = r.album;
            item.source = "NetEase";
            item.id = r.id;
            item.duration = r.duration;
            results.append(item);
        }
    } catch (...) {
        qCWarning(dmMusic) << "NetEase search exception";
    }

    // Search LRCLIB
    QUrl searchUrl("https://lrclib.net/api/search");
    QUrlQuery query;
    query.addQueryItem("q", keyword);
    searchUrl.setQuery(query);

    QByteArray searchResponse = httpGet(searchUrl.toString());
    if (!searchResponse.isEmpty()) {
        QJsonDocument searchDoc = QJsonDocument::fromJson(searchResponse);
        if (searchDoc.isArray()) {
            QJsonArray searchResults = searchDoc.array();
            for (const auto &r : searchResults) {
                QJsonObject obj = r.toObject();
                LyricSearchResult item;
                item.title = obj.value("trackName").toString();
                item.artist = obj.value("artistName").toString();
                item.album = obj.value("albumName").toString();
                item.source = "LRCLIB";
                item.duration = static_cast<qint64>(obj.value("duration").toDouble() * 1000);
                item.lrclibTrackName = item.title;
                item.lrclibArtistName = item.artist;
                item.lrclibAlbumName = item.album;
                results.append(item);
            }
        }
    }

    return results;
}

QString LyricDownloader::getLyricsFromNetEase(qint64 songId)
{
    try {
        return m_neApi->getLyrics(songId);
    } catch (...) {
        qCWarning(dmMusic) << "NetEase getLyrics exception";
        return QString();
    }
}

QString LyricDownloader::getLyricsFromLrclib(const LyricSearchResult &result)
{
    QUrl lyricUrl("https://lrclib.net/api/get");
    QUrlQuery lyricQuery;
    lyricQuery.addQueryItem("track_name", result.lrclibTrackName);
    lyricQuery.addQueryItem("artist_name", result.lrclibArtistName);
    lyricQuery.addQueryItem("album_name", result.lrclibAlbumName);
    lyricQuery.addQueryItem("duration", QString::number(result.duration / 1000.0, 'f', 1));
    lyricUrl.setQuery(lyricQuery);

    QByteArray lyricResponse = httpGet(lyricUrl.toString());
    if (lyricResponse.isEmpty()) return QString();

    QJsonDocument lyricDoc = QJsonDocument::fromJson(lyricResponse);
    if (!lyricDoc.isObject()) return QString();

    QJsonObject lyricObj = lyricDoc.object();
    if (lyricObj.contains("error")) return QString();

    QString lyricsText;
    if (lyricObj.contains("syncedLyrics") && !lyricObj.value("syncedLyrics").toString().isEmpty())
        lyricsText = lyricObj.value("syncedLyrics").toString();
    else if (lyricObj.contains("plainLyrics") && !lyricObj.value("plainLyrics").toString().isEmpty())
        lyricsText = lyricObj.value("plainLyrics").toString();

    return lyricsText;
}

QString LyricDownloader::getLyricsFromKugou(const LyricSearchResult &result)
{
    try {
        KgsearchResult kgResult;
        kgResult.hash = result.kugouHash;
        kgResult.id = result.id;
        kgResult.title = result.title;
        kgResult.artist = result.artist;
        kgResult.album = result.album;
        kgResult.duration = result.duration;
        return m_kgApi->getLyrics(kgResult);
    } catch (...) {
        qCWarning(dmMusic) << "Kugou getLyrics exception";
        return QString();
    }
}
