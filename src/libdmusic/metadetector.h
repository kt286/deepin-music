/*
 * Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co., Ltd.
 *
 * Author:     ZouYa <zouya@uniontech.com>
 *
 * Maintainer: WangYu <wangyu@uniontech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <QImage>

#include "util/singleton.h"

#include <mediameta.h>

class QFileInfo;
class LIBDMUSICSHARED_EXPORT MetaDetector: public QObject, public DMusic::DSingleton<MetaDetector>
{
public:
    void init();

    static QList<QByteArray> detectEncodings(const MediaMeta &meta);
    static QList<QByteArray> detectEncodings(const QByteArray &rawData);

    void updateCueFileTagCodec(MediaMeta &meta, const QFileInfo &, const QByteArray &codec);

    MediaMeta updateMetaFromLocalfile(MediaMeta meta, const QFileInfo &fileInfo, int engineType);
    MediaMeta updateMediaFileTagCodec(MediaMeta &meta, const QByteArray &codecName, bool forceEncode);

    static void getCoverData(const QString &path, const QString &tmpPath, const QString &hash, int engineType);
    static QPixmap getCoverDataPixmap(MediaMeta meta, int engineType);

    static void getLyricData(const QString &path, const QString &tmpPath, const QString &hash);
    //static QVector<float> getMetaData(const QString &path);

private:
    explicit MetaDetector();
    friend class DMusic::DSingleton<MetaDetector>;
};
