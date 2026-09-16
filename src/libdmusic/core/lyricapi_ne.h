// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LYRICAPI_NE_H
#define LYRICAPI_NE_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>

#include "global.h"

struct NesearchResult {
    qint64 id = 0;
    QString title;
    QString artist;
    QString album;
    qint64 duration = 0;
};

class NetEaseApi : public QObject
{
    Q_OBJECT
public:
    explicit NetEaseApi(QObject *parent = nullptr);
    ~NetEaseApi();

    QList<NesearchResult> searchSongs(const QString &keyword);
    QString getLyrics(qint64 songId);
    QString searchAndGetLyrics(const DMusic::MediaMeta &meta);

private:
    QByteArray eapiEncrypt(const QByteArray &path, const QVariantMap &params);
    QByteArray eapiDecrypt(const QByteArray &data);
    QByteArray makeRequest(const QByteArray &path, const QVariantMap &params);

    QNetworkAccessManager *m_netManager = nullptr;
};

#endif // LYRICAPI_NE_H
