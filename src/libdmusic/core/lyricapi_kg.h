// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LYRICAPI_KG_H
#define LYRICAPI_KG_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>

#include "global.h"

struct KgsearchResult {
    qint64 id = 0;
    QString hash;
    QString title;
    QString artist;
    QString album;
    qint64 duration = 0; // ms
};

class KugouApi : public QObject
{
    Q_OBJECT
public:
    explicit KugouApi(QObject *parent = nullptr);
    ~KugouApi();

    QList<KgsearchResult> searchSongs(const QString &keyword);
    QString getLyrics(const KgsearchResult &song);
    QString searchAndGetLyrics(const DMusic::MediaMeta &meta);

private:
    static QByteArray krcDecrypt(const QByteArray &encrypted);
    QByteArray computeSignature(const QVariantMap &params, const QByteArray &postData = QByteArray());
    QByteArray httpGet(const QString &url, const QMap<QByteArray, QByteArray> &headers = {});

    QNetworkAccessManager *m_netManager = nullptr;
    QString m_dfid;
};

#endif // LYRICAPI_KG_H
