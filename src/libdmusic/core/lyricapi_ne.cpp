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
#include <QDebug>

#include "util/log.h"

static const QByteArray NE_AES_KEY = "e82ckenh8dichen8";
static const QByteArray NE_XOR_KEY = "3go8&$8*3*3h0k(2)2";
static const QByteArray NE_SIGN_PFX = "nobody";
static const QByteArray NE_SIGN_SFX = "md5forencrypt";
static const QByteArray NE_SEP = "-36cd479b6b5-";
static const QString NE_BASE = "https://interface.music.163.com";
static const QString NE_APPVER = "3.1.3.203419";

static const QStringList NE_MODES = {
    "MS-iCraft B760M WIFI",
    "ASUS ROG STRIX Z790",
    "MSI MAG B550 TOMAHAWK",
    "ASRock X670E Taichi"
};

static QByteArray randomHex(int len)
{
    QByteArray r;
    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < len; ++i)
        r += QByteArray::number(rng->bounded(16), 16).toUpper();
    return r;
}

static QString randomAlpha(int len)
{
    const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    QString r;
    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < len; ++i)
        r += chars.at(rng->bounded(chars.size()));
    return r;
}

static QByteArray randomMac()
{
    QStringList parts;
    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < 6; ++i)
        parts << QString("%1").arg(rng->bounded(256), 2, 16, QChar('0')).toUpper();
    return parts.join(":").toUtf8();
}

NetEaseApi::NetEaseApi(QObject *parent)
    : QObject(parent)
    , m_netManager(new QNetworkAccessManager(this))
{
}

NetEaseApi::~NetEaseApi() = default;

QByteArray NetEaseApi::buildCookieBytes() const
{
    QStringList pairs;
    pairs << "WEVNSM=1.0.0"
          << "os=pc"
          << "deviceId=" + m_deviceId
          << "osver=" + m_osver
          << "clientSign=" + m_clientSign
          << "channel=netease"
          << "mode=" + m_mode
          << "appver=" + NE_APPVER;
    if (!m_nmtid.isEmpty()) pairs << "NMTID=" + m_nmtid;
    if (!m_musicA.isEmpty()) pairs << "MUSIC_A=" + m_musicA;
    if (!m_csrf.isEmpty())   pairs << "__csrf=" + m_csrf;
    // WNMCID
    auto *rng = QRandomGenerator::global();
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QString wnmcid = randomAlpha(6) + "." + QString::number(now - rng->bounded(9000) + 1000) + ".01.0";
    pairs << "WNMCID=" + wnmcid;
    return pairs.join("; ").toUtf8();
}

void NetEaseApi::ensureLogin()
{
    if (m_loggedIn) return;

    // Generate device identifiers
    m_deviceId = QString::fromLatin1(randomHex(48));
    QByteArray mac = randomMac();
    QString randStr = randomAlpha(8);
    m_clientSign = QString("%1@@@%2@@@@@@%3").arg(QString::fromUtf8(mac), randStr, QString::fromLatin1(randomHex(32)));
    auto *rng = QRandomGenerator::global();
    m_osver = QString("Microsoft-Windows-10--build%100-64bit").arg(rng->bounded(100) + 200);
    m_mode = NE_MODES.at(rng->bounded(NE_MODES.size()));

    // Compute anonymous username: XOR(deviceId, key) -> MD5 -> base64
    QByteArray xored;
    for (int i = 0; i < m_deviceId.size(); ++i)
        xored += static_cast<char>(m_deviceId.at(i).toLatin1() ^ NE_XOR_KEY.at(i % NE_XOR_KEY.size()));
    QByteArray md5Raw = QCryptographicHash::hash(xored, QCryptographicHash::Md5);
    QString b64Md5 = QString::fromLatin1(md5Raw.toBase64());
    QString username = QString::fromLatin1((m_deviceId.toUtf8() + " " + b64Md5.toUtf8()).toBase64());

    // Build header and params
    QJsonObject hdr;
    hdr["clientSign"] = m_clientSign;
    hdr["os"] = "pc";
    hdr["appver"] = NE_APPVER;
    hdr["deviceId"] = m_deviceId;
    hdr["requestId"] = 0;
    hdr["osver"] = m_osver;

    QVariantMap params;
    params["username"] = username;
    params["e_r"] = true;
    params["header"] = QString::fromUtf8(QJsonDocument(hdr).toJson(QJsonDocument::Compact));

    QByteArray encPath = "/api/register/anonimous";
    QByteArray encParams = eapiEncrypt(encPath, params);

    QUrl url(NE_BASE + "/eapi/register/anonimous");
    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Safari/537.36 Chrome/91.0.4472.164 NeteaseMusicDesktop/" + NE_APPVER.toUtf8());
    request.setRawHeader("Origin", "orpheus://orpheus");
    request.setRawHeader("Referer", "orpheus://orpheus");
    request.setRawHeader("Accept", "*/*");
    request.setRawHeader("Accept-Encoding", "gzip, deflate, br");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");

    QByteArray body = "params=" + encParams.toHex().toUpper();
    QNetworkReply *reply = m_netManager->post(request, body);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.start(10000);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray rawResp = reply->readAll();

        QByteArray respData;
        if (!rawResp.isEmpty() && rawResp.at(0) == '{') {
            respData = rawResp;
        } else {
            respData = eapiDecrypt(rawResp);
        }

        QJsonDocument doc = QJsonDocument::fromJson(respData);
        QJsonObject obj = doc.object();
        int code = obj.value("code").toInt();
        QString errMsg = obj.value("message").toString();
        if (errMsg.isEmpty()) errMsg = obj.value("msg").toString();
        if (code == 200) {
            m_userId = obj.value("userId").toVariant().toString();
            // Extract Set-Cookie values from reply
            QList<QPair<QByteArray, QByteArray>> headers = reply->rawHeaderPairs();
            for (const auto &pair : headers) {
                if (pair.first.toLower() == "set-cookie") {
                    QString cookieStr = QString::fromUtf8(pair.second);
                    if (cookieStr.contains("NMTID="))
                        m_nmtid = cookieStr.section("NMTID=", 1).section(";", 0);
                    if (cookieStr.contains("MUSIC_A="))
                        m_musicA = cookieStr.section("MUSIC_A=", 1).section(";", 0);
                    if (cookieStr.contains("__csrf="))
                        m_csrf = cookieStr.section("__csrf=", 1).section(";", 0);
                }
            }
            m_loggedIn = true;
            qCInfo(dmMusic) << "NetEase anonymous login OK, userId:" << m_userId;
        } else {
            qCWarning(dmMusic) << "NetEase login failed:" << obj.value("msg").toString();
        }
    } else {
        qCWarning(dmMusic) << "NE login HTTP error:" << reply->errorString()
                           << "status:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    }
    reply->deleteLater();
}

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
    // Try without login first; login is only needed for some endpoints
    // ensureLogin();

    QJsonObject hdr;
    if (m_loggedIn) {
        hdr["clientSign"] = m_clientSign;
        hdr["os"] = "pc";
        hdr["appver"] = NE_APPVER;
        hdr["deviceId"] = m_deviceId;
        hdr["requestId"] = 0;
        hdr["osver"] = m_osver;
    } else {
        // Minimal header for unauthenticated requests
        QByteArray fakeDeviceId = randomHex(48);
        hdr["os"] = "pc";
        hdr["appver"] = NE_APPVER;
        hdr["deviceId"] = QString::fromLatin1(fakeDeviceId);
        hdr["requestId"] = 0;
        hdr["osver"] = "Microsoft-Windows-10--build22000-64bit";
    }

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
    if (m_loggedIn) {
        request.setRawHeader("Cookie", buildCookieBytes());
    }
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

QString NetEaseApi::getLyrics(qint64 songId)
{
    QVariantMap params;
    params["id"] = QString::number(songId);
    params["lv"] = "-1";
    params["tv"] = "-1";
    params["rv"] = "-1";
    params["yv"] = "-1";

    QByteArray resp = makeRequest("/eapi/song/lyric/v1", params);
    qCInfo(dmMusic) << "NE lyrics raw response size:" << resp.size();
    QJsonDocument doc = QJsonDocument::fromJson(resp);
    QJsonObject root = doc.object();
    if (root.value("code").toInt() != 200) return QString();

    // Helper: parse mixed format {"t":0,"c":[{"tx":"..."}]}\n[mm:ss.xx]text
    // The lyric field can contain: JSON metadata lines + LRC lines separated by \n
    auto parseLyricField = [](const QString &text) -> QString {
        if (text.isEmpty()) return QString();

        // Split by newlines - each line is either a JSON object or LRC text
        QStringList lines = text.split('\n');
        QString result;
        for (const QString &line : lines) {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;

            if (trimmed.startsWith('{')) {
                // JSON metadata line like {"t":0,"c":[{"tx":"作词: "},...]}
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
                // Plain text line (LRC format like [00:29.36]text)
                if (!result.isEmpty()) result += '\n';
                result += trimmed;
            }
        }
        return result;
    };

    QString yrcText = parseLyricField(root.value("yrc").toObject().value("lyric").toString());
    QString lrcText = parseLyricField(root.value("lrc").toObject().value("lyric").toString());
    QString transText = parseLyricField(root.value("tlyric").toObject().value("lyric").toString());

    qCInfo(dmMusic) << "NE lyrics - lrc size:" << lrcText.size() << "yrc size:" << yrcText.size();

    // Prefer standard LRC over YRC (逐字格式 needs special parser)
    QString result;
    if (!lrcText.isEmpty())
        result = lrcText;
    else if (!yrcText.isEmpty())
        result = yrcText;

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

    qCWarning(dmMusic) << "NE search keyword:" << keyword << "results:" << results.size();
    for (int i = 0; i < qMin(5, results.size()); ++i) {
        qCWarning(dmMusic) << "  [" << i << "]" << results[i].title << "-" << results[i].artist << "id:" << results[i].id;
    }

    int bestIdx = -1;
    for (int i = 0; i < results.size(); ++i) {
        bool titleExact = results[i].title.compare(meta.title, Qt::CaseInsensitive) == 0;
        bool titleContains = results[i].title.contains(meta.title, Qt::CaseInsensitive)
                             || meta.title.contains(results[i].title, Qt::CaseInsensitive);
        bool artistOk = meta.artist.isEmpty()
                        || results[i].artist.contains(meta.artist, Qt::CaseInsensitive)
                        || meta.artist.contains(results[i].artist, Qt::CaseInsensitive);
        // Exact title match preferred
        if (titleExact && artistOk) { bestIdx = i; break; }
        // Fuzzy title match (both directions) with artist match
        if (bestIdx < 0 && titleContains && artistOk) { bestIdx = i; }
    }
    if (bestIdx < 0) bestIdx = 0;

    qCWarning(dmMusic) << "NE matched:" << results[bestIdx].title << "-" << results[bestIdx].artist << "id:" << results[bestIdx].id;
    return getLyrics(results[bestIdx].id);
}
