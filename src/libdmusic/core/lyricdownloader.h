// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LYRICDOWNLOADER_H
#define LYRICDOWNLOADER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVariantList>

#include "global.h"

class NetEaseApi;
class KugouApi;

struct LyricSearchResult {
    QString title;
    QString artist;
    QString album;
    QString source;    // "NetEase" or "LRCLIB"
    qint64 id = 0;     // NetEase song ID
    qint64 duration = 0;
    // LRCLIB fields
    QString lrclibTrackName;
    QString lrclibArtistName;
    QString lrclibAlbumName;
};

class LyricDownloader : public QObject
{
    Q_OBJECT
public:
    explicit LyricDownloader(QObject *parent = nullptr);
    ~LyricDownloader();

    // Download lyrics from multiple sources, save to savePath.
    // Returns lyrics text on success, empty on failure.
    // savedPath receives the actual path where file was written.
    QString downloadAndSaveLyrics(const DMusic::MediaMeta &meta,
                                  const QString &savePath,
                                  QString *savedPath = nullptr);

    // Search for lyrics from multiple sources
    QList<LyricSearchResult> searchLyrics(const QString &keyword);
    // Get lyrics by NetEase song ID
    QString getLyricsFromNetEase(qint64 songId);
    // Get lyrics from LRCLIB by search result
    QString getLyricsFromLrclib(const LyricSearchResult &result);

private:
    // Try each source in order
    QString fetchFromNetEase(const DMusic::MediaMeta &meta);
    QString fetchFromKugou(const DMusic::MediaMeta &meta);
    QString fetchFromLrclib(const DMusic::MediaMeta &meta);
    QString fetchLyricsFromLrclib(const DMusic::MediaMeta &meta);

    QByteArray httpGet(const QString &url);

    QNetworkAccessManager *m_netManager;
    NetEaseApi *m_neApi;
    KugouApi *m_kgApi;
};

#endif // LYRICDOWNLOADER_H
