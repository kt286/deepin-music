// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lyricapi_ne.h"
#include "lyriccrypt.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QDebug>

#include "util/log.h"

static const QByteArray NE_AES_KEY = "e82ckenh8dichen8";
static const QByteArray NE_SIGN_PFX = "nobody";
static const QByteArray NE_SIGN_SFX = "md5forencrypt";
static const QByteArray NE_SEP = "-36cd479b6b5-";
static const QString NE_BASE = "https://interface.music.163.com";
static const QString NE_APPVER = "3.1.3.203419";

static QByteArray randomHex(int len)
{
    QByteArray r;
    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < len; ++i)
        r += QByteArray::number(rng->bounded(16), 16).toUpper();
    return r;
}

NetEaseApi::NetEaseApi(QObject *parent)
    : QObject(parent)
    , m_netManager(new QNetworkAccessManager(this))
{
}

NetEaseApi::~NetEaseApi() = default;

QByteArray NetEaseApi::eapiEncrypt(const QByteArray &path, const QVariantMap &params)
{
    QByteArray pBytes = QJsonDocument(QJsonObject::fromVariantMap(params)).toJson(QJsonDocument::Compact);
    QByteArray signSrc = NE_SIGN_PFX + path + QByteArray("use") + pBytes + NE_SIGN_SFX;
    QByteArray sign = LyricCrypt::md5(signSrc).toLatin1();
    QByteArray aesSrc = path + NE_SEP + pBytes + NE_SEP + sign;
    return LyricCrypt::aesEcbEncrypt(aesSrc, NE_AES_KEY);
}

QByteArray NetEaseApi::eapiDecrypt(const QByteArray &data)
{
    return LyricCrypt::aesEcbDecrypt(data, NE_AES_KEY);
}

QByteArray NetEaseApi::makeRequest(const QByteArray &path, const QVariantMap &params)
{
    QJsonObject hdr;
    QByteArray fakeDeviceId = randomHex(48);
    hdr["os"] = "pc";
    hdr["appver"] = NE_APPVER;
    hdr["deviceId"] = QString::fromLatin1(fakeDeviceId);
    hdr["requestId"] = 0;
    hdr["osver"] = "Microsoft-Windows-10--build22000-64bit";

    QVariantMap reqParams = params;
    reqParams["e_r"] = true;
    reqParams["header"] = QString::fromUtf8(QJsonDocument(hdr).toJson(QJsonDocument::Compact));

    QByteArray apiPath = path;
    apiPath.replace("eapi", "api");
    QByteArray encParams = eapiEncrypt(apiPath, reqParams);

    QUrl url(NE_BASE + QString::fromUtf8(path));
    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Safari/537.36 Chrome/91.0.4472.164 NeteaseMusicDesktop/" + NE_APPVER.toUtf8());
    request.setRawHeader("Origin", "orpheus://orpheus");

    QByteArray body = "params=" + encParams.toHex().toUpper();
    QNetworkReply *reply = m_netManager->post(request, body);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.start(15000);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    loop.exec();

    QByteArray result;
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray raw = reply->readAll();
        if (!raw.isEmpty() && raw.at(0) == '{') {
            result = raw;
        } else {
            result = eapiDecrypt(raw);
        }
    } else {
        qCWarning(dmMusic) << "NetEase HTTP error for" << path << ":" << reply->errorString();
    }
    reply->deleteLater();
    return result;
}

QList<NesearchResult> NetEaseApi::searchSongs(const QString &keyword)
{
    QVariantMap params;
    params["limit"] = "20";
    params["offset"] = "0";
    params["keyword"] = keyword;
    params["scene"] = "NORMAL";
    params["needCorrect"] = "true";

    QByteArray resp = makeRequest("/eapi/search/song/list/page", params);
    QJsonDocument doc = QJsonDocument::fromJson(resp);
    QJsonObject root = doc.object();

    QList<NesearchResult> results;
    if (root.value("code").toInt() != 200) return results;

    QJsonArray resources = root.value("data").toObject().value("resources").toArray();
    for (const auto &r : resources) {
        QJsonObject song = r.toObject().value("baseInfo").toObject().value("simpleSongData").toObject();
        NesearchResult item;
        item.id = song.value("id").toVariant().toLongLong();
        item.title = song.value("name").toString();
        QStringList artists;
        for (const auto &a : song.value("ar").toArray())
            artists << a.toObject().value("name").toString();
        item.artist = artists.join(", ");
        item.album = song.value("al").toObject().value("name").toString();
        item.duration = song.value("dt").toVariant().toLongLong();
        results.append(item);
    }
    return results;
}

// 将毫秒转换为标准LRC时间戳格式 mm:ss.xx
static QString msToLrcTime(qint64 ms)
{
    qint64 totalSeconds = ms / 1000;
    qint64 minutes = totalSeconds / 60;
    qint64 seconds = totalSeconds % 60;
    qint64 centiseconds = (ms % 1000) / 10;
    return QString("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(centiseconds, 2, 10, QChar('0'));
}

// 将网易云YRC格式转换为标准逐字LRC格式
static QString convertYrcToStandardLrc(const QString &yrcText)
{
    if (yrcText.isEmpty()) return QString();

    QStringList lines = yrcText.split('\n');
    QString result;

    QRegularExpression lineRegex("^\\[(\\d+),(\\d+)\\](.*)$");
    QRegularExpression wordRegex("\\((\\d+),(\\d+),\\d+\\)([^\\(]*)");

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || !trimmed.startsWith('[')) continue;

        QRegularExpressionMatch lineMatch = lineRegex.match(trimmed);
        if (!lineMatch.hasMatch()) continue;

        qint64 lineStart = lineMatch.captured(1).toLongLong();
        QString content = lineMatch.captured(3);

        QString lrcLine = "[" + msToLrcTime(lineStart) + "]";
        bool hasWords = false;
        QRegularExpressionMatchIterator wordIt = wordRegex.globalMatch(content);

        while (wordIt.hasNext()) {
            QRegularExpressionMatch wordMatch = wordIt.next();
            qint64 wordStart = lineStart + wordMatch.captured(1).toLongLong();
            QString wordText = wordMatch.captured(3);

            if (!wordText.isEmpty()) {
                lrcLine += "[" + msToLrcTime(wordStart) + "]" + wordText;
                hasWords = true;
            }
        }

        if (hasWords) {
            if (!result.isEmpty()) result += '\n';
            result += lrcLine;
        } else {
            if (!result.isEmpty()) result += '\n';
            result += "[" + msToLrcTime(lineStart) + "]" + content;
        }
    }

    return result;
}

QString NetEaseApi::getLyrics(qint64 songId)
{
    QVariantMap params;
    params["id"] = QString::number(songId);
    params["lv"] = "-1";
    params["tv"] = "-1";
    params["rv"] = "-1";
    params["yv"] = "-1";

    QByteArray resp = makeRequest("/eapi/song/lyric/v1", params);
    QJsonDocument doc = QJsonDocument::fromJson(resp);
    QJsonObject root = doc.object();
    if (root.value("code").toInt() != 200) return QString();

    auto parseLyricField = [](const QString &text) -> QString {
        if (text.isEmpty()) return QString();

        QStringList lines = text.split('\n');
        QString result;
        for (const QString &line : lines) {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;

            if (trimmed.startsWith('{')) {
                QByteArray wrapped = "\"" + trimmed.toUtf8() + "\"";
                QJsonDocument wrapDoc = QJsonDocument::fromJson(wrapped);
                if (!wrapDoc.isNull()) {
                    QString unescaped = wrapDoc.toVariant().toString();
                    if (!unescaped.isEmpty()) {
                        QJsonDocument innerDoc = QJsonDocument::fromJson(unescaped.toUtf8());
                        QJsonObject innerObj = innerDoc.object();
                        if (innerObj.contains("c")) {
                            QJsonArray cArr = innerObj.value("c").toArray();
                            QString lineText;
                            for (const auto &item : cArr) {
                                lineText += item.toObject().value("tx").toString();
                            }
                            if (!lineText.isEmpty()) {
                                if (!result.isEmpty()) result += '\n';
                                result += lineText;
                            }
                        } else {
                            if (!result.isEmpty()) result += '\n';
                            result += unescaped;
                        }
                    }
                }
            } else {
                if (!result.isEmpty()) result += '\n';
                result += trimmed;
            }
        }
        return result;
    };

    QString yrcText = parseLyricField(root.value("yrc").toObject().value("lyric").toString());
    QString lrcText = parseLyricField(root.value("lrc").toObject().value("lyric").toString());
    QString transText = parseLyricField(root.value("tlyric").toObject().value("lyric").toString());

    QString convertedYrc;
    if (!yrcText.isEmpty()) {
        convertedYrc = convertYrcToStandardLrc(yrcText);
    }

    QString result;
    if (!convertedYrc.isEmpty())
        result = convertedYrc;
    else if (!lrcText.isEmpty())
        result = lrcText;

    if (!transText.isEmpty() && !result.isEmpty())
        result += "\n" + transText;

    return result;
}

QString NetEaseApi::searchAndGetLyrics(const DMusic::MediaMeta &meta)
{
    QString keyword = !meta.artist.isEmpty()
        ? meta.title + " " + meta.artist : meta.title;

    QList<NesearchResult> results = searchSongs(keyword);
    if (results.isEmpty()) return QString();

    int bestIdx = -1;
    for (int i = 0; i < results.size(); ++i) {
        bool titleExact = results[i].title.compare(meta.title, Qt::CaseInsensitive) == 0;
        bool titleContains = results[i].title.contains(meta.title, Qt::CaseInsensitive)
                             || meta.title.contains(results[i].title, Qt::CaseInsensitive);
        bool artistOk = meta.artist.isEmpty()
                        || results[i].artist.contains(meta.artist, Qt::CaseInsensitive)
                        || meta.artist.contains(results[i].artist, Qt::CaseInsensitive);
        if (titleExact && artistOk) { bestIdx = i; break; }
        if (bestIdx < 0 && titleContains && artistOk) { bestIdx = i; }
    }
    if (bestIdx < 0) bestIdx = 0;

    return getLyrics(results[bestIdx].id);
}
