// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lyricapi_kg.h"
#include "lyriccrypt.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QRandomGenerator>
#include <QDateTime>
#include <QDebug>

#include "util/log.h"

static const QByteArray KG_HMAC_KEY = "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA";
static const QByteArray KG_KRC_KEY = "@Gaw^2tGQ61-\xce\xd2ni";
static const QString KG_SEARCH_URL = "http://complexsearch.kugou.com/v2/search/song";
static const QString KG_LYRICS_LIST_URL = "https://lyrics.kugou.com/v1/search";
static const QString KG_LYRICS_DL_URL = "http://lyrics.kugou.com/download";
static const QString KG_REGISTER_URL = "https://userservice.kugou.com/risk/v1/r_register_dev";

static QByteArray kgMid()
{
    qint64 ms = QDateTime::currentMSecsSinceEpoch();
    return LyricCrypt::md5(QByteArray::number(ms)).toUtf8();
}

KugouApi::KugouApi(QObject *parent)
    : QObject(parent)
    , m_netManager(new QNetworkAccessManager(this))
{
}

KugouApi::~KugouApi() = default;

QByteArray KugouApi::krcDecrypt(const QByteArray &encrypted)
{
    if (encrypted.size() <= 4) return QByteArray();
    // Skip 4-byte magic header
    QByteArray data = encrypted.mid(4);
    // XOR with key
    QByteArray decrypted(data.size(), 0);
    for (int i = 0; i < data.size(); ++i)
        decrypted[i] = data.at(i) ^ KG_KRC_KEY.at(i % KG_KRC_KEY.size());
    // zlib decompress
    return LyricCrypt::zlibDecompress(decrypted);
}

QByteArray KugouApi::computeSignature(const QVariantMap &params, const QByteArray &postData)
{
    // Sort params by key, format as key=value, concatenate, compute MD5
    QStringList keys = params.keys();
    keys.sort();
    QString concat;
    for (const QString &k : keys) {
        QVariant v = params.value(k);
        if (v.canConvert<QVariantMap>()) {
            concat += k + "=" + QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(v.toMap())).toJson(QJsonDocument::Compact));
        } else {
            concat += k + "=" + v.toString();
        }
    }
    concat += QString::fromUtf8(postData);
    QByteArray signSrc = KG_HMAC_KEY + concat.toUtf8() + KG_HMAC_KEY;
    return LyricCrypt::md5(signSrc).toUtf8();
}

QByteArray KugouApi::httpGet(const QString &url, const QMap<QByteArray, QByteArray> &headers)
{
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("User-Agent", "Android14-1070-11070-201-0-Lyric-wifi");
    request.setRawHeader("Connection", "Keep-Alive");
    request.setRawHeader("KG-Rec", "1");
    request.setRawHeader("KG-RC", "1");
    request.setRawHeader("KG-CLIENTTIMEMS", QByteArray::number(QDateTime::currentMSecsSinceEpoch()));
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        request.setRawHeader(it.key(), it.value());

    QNetworkReply *reply = m_netManager->get(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.start(15000);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    loop.exec();

    QByteArray result;
    if (reply->error() == QNetworkReply::NoError) {
        result = reply->readAll();
    } else {
        qCWarning(dmMusic) << "Kugou HTTP GET error:" << url << reply->errorString();
    }
    reply->deleteLater();
    return result;
}

QList<KgsearchResult> KugouApi::searchSongs(const QString &keyword)
{
    QByteArray mid = kgMid();

    QVariantMap params;
    params["appid"] = "3116";
    params["clientver"] = "11070";
    params["clienttime"] = QString::number(QDateTime::currentSecsSinceEpoch());
    params["iscorrection"] = "1";
    params["uuid"] = "-";
    params["mid"] = QString::fromUtf8(mid);
    params["dfid"] = "-";
    params["platform"] = "AndroidFilter";
    params["userid"] = "0";
    params["token"] = "";
    params["keyword"] = keyword;
    params["page"] = "1";
    params["pagesize"] = "20";
    params["sorttype"] = "0";

    params["signature"] = QString::fromUtf8(computeSignature(params));

    // Build URL
    QUrl url(KG_SEARCH_URL);
    QUrlQuery query;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        query.addQueryItem(it.key(), it.value().toString());
    url.setQuery(query);

    QMap<QByteArray, QByteArray> headers;
    headers["x-router"] = "complexsearch.kugou.com";
    headers["mid"] = mid;

    QByteArray resp = httpGet(url.toString(), headers);
    QJsonDocument doc = QJsonDocument::fromJson(resp);
    QJsonObject root = doc.object();

    QList<KgsearchResult> results;
    if (root.value("error_code").toInt() != 0) return results;

    QJsonArray lists = root.value("data").toObject().value("lists").toArray();
    for (const auto &item : lists) {
        QJsonObject obj = item.toObject();
        KgsearchResult r;
        r.id = obj.value("ID").toVariant().toLongLong();
        r.hash = obj.value("FileHash").toString();
        r.title = obj.value("SongName").toString();
        QStringList singers;
        for (const auto &s : obj.value("Singers").toArray())
            singers << s.toObject().value("name").toString();
        r.artist = singers.join(", ");
        r.album = obj.value("AlbumName").toString();
        r.duration = static_cast<qint64>(obj.value("Duration").toDouble()) * 1000;
        results.append(r);
    }
    return results;
}

QString KugouApi::getLyrics(const KgsearchResult &song)
{
    QByteArray mid = kgMid();

    // Step 1: Get lyrics candidates
    QVariantMap listParams;
    listParams["appid"] = "3116";
    listParams["clientver"] = "11070";
    listParams["album_audio_id"] = QString::number(song.id);
    listParams["duration"] = QString::number(song.duration);
    listParams["hash"] = song.hash;
    listParams["keyword"] = song.artist + " - " + song.title;
    listParams["lrctxt"] = "1";
    listParams["man"] = "no";

    listParams["signature"] = QString::fromUtf8(computeSignature(listParams));

    QUrl listUrl(KG_LYRICS_LIST_URL);
    QUrlQuery listQuery;
    for (auto it = listParams.constBegin(); it != listParams.constEnd(); ++it)
        listQuery.addQueryItem(it.key(), it.value().toString());
    listUrl.setQuery(listQuery);

    QMap<QByteArray, QByteArray> headers;
    headers["mid"] = mid;

    QByteArray listResp = httpGet(listUrl.toString(), headers);
    QJsonDocument listDoc = QJsonDocument::fromJson(listResp);
    QJsonObject listRoot = listDoc.object();

    if (listRoot.value("error_code").toInt() != 0) return QString();
    QJsonArray candidates = listRoot.value("candidates").toArray();
    if (candidates.isEmpty()) return QString();

    // Pick best candidate (highest score)
    QJsonObject best = candidates.first().toObject();
    for (const auto &c : candidates) {
        QJsonObject obj = c.toObject();
        if (obj.value("score").toInt() > best.value("score").toInt())
            best = obj;
    }

    QString lyricsId = best.value("id").toString();
    QString accessKey = best.value("accesskey").toString();

    // Step 2: Download lyrics
    QVariantMap dlParams;
    dlParams["appid"] = "3116";
    dlParams["clientver"] = "11070";
    dlParams["accesskey"] = accessKey;
    dlParams["charset"] = "utf8";
    dlParams["client"] = "mobi";
    dlParams["fmt"] = "krc";
    dlParams["id"] = lyricsId;
    dlParams["ver"] = "1";

    dlParams["signature"] = QString::fromUtf8(computeSignature(dlParams));

    QUrl dlUrl(KG_LYRICS_DL_URL);
    QUrlQuery dlQuery;
    for (auto it = dlParams.constBegin(); it != dlParams.constEnd(); ++it)
        dlQuery.addQueryItem(it.key(), it.value().toString());
    dlUrl.setQuery(dlQuery);

    QByteArray dlResp = httpGet(dlUrl.toString(), headers);
    QJsonDocument dlDoc = QJsonDocument::fromJson(dlResp);
    QJsonObject dlRoot = dlDoc.object();

    if (dlRoot.value("error_code").toInt() != 0) return QString();

    int contentType = dlRoot.value("contenttype").toInt();
    QByteArray contentB64 = dlRoot.value("content").toString().toUtf8();
    QByteArray content = QByteArray::fromBase64(contentB64);

    if (contentType == 2) {
        // Plain text
        return QString::fromUtf8(content);
    } else {
        // Encrypted KRC
        return QString::fromUtf8(krcDecrypt(content));
    }
}

QString KugouApi::searchAndGetLyrics(const DMusic::MediaMeta &meta)
{
    QString keyword = !meta.artist.isEmpty()
        ? meta.artist + " " + meta.title : meta.title;

    QList<KgsearchResult> results = searchSongs(keyword);
    if (results.isEmpty()) return QString();

    int bestIdx = 0;
    for (int i = 0; i < results.size(); ++i) {
        bool titleOk = results[i].title.compare(meta.title, Qt::CaseInsensitive) == 0
                       || results[i].title.contains(meta.title, Qt::CaseInsensitive)
                       || meta.title.contains(results[i].title, Qt::CaseInsensitive);
        bool artistOk = meta.artist.isEmpty()
                        || results[i].artist.contains(meta.artist, Qt::CaseInsensitive)
                        || meta.artist.contains(results[i].artist, Qt::CaseInsensitive);
        if (titleOk && artistOk) { bestIdx = i; break; }
    }

    qCInfo(dmMusic) << "Kugou matched:" << results[bestIdx].title << "-" << results[bestIdx].artist;
    return getLyrics(results[bestIdx]);
}
