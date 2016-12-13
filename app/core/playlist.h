/**
 * Copyright (C) 2016 Deepin Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 **/

#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <QObject>
#include <QSharedPointer>

#include "music.h"

class PlaylistMeta
{
public:
    PlaylistMeta()
    {
        editmode = false;
        readonly = false;
        hide = false;
        sortType = 0;
    }

    QString uuid;
    QString displayName;
    QString url;
    QString icon;

    int     sortType;
    bool    editmode;
    bool    readonly;
    bool    hide;
    bool    unused;

    QStringList                 musicIds;
    QMap<QString, MusicMeta>    musicMap;
};

Q_DECLARE_METATYPE(PlaylistMeta);


class Playlist : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString displayName READ displayName WRITE setDisplayName NOTIFY displayNameChanged)
public:
    explicit Playlist(const PlaylistMeta &musiclistinfo, QObject *parent = 0);

    enum PlayMode {
        Order  = 0,
        RepeatList,
        Repeat,
        Shufflt
    };
    Q_ENUM(PlayMode)

    enum SortType {
        SortByAddTime  = 0,
        SortByTitle,
        SortByArtist,
        SortByAblum
    };
    Q_ENUM(SortType)

public:
    //! public interface
    QString id();
    QString displayName();
    QString icon();
    bool readonly();
    bool editmode();
    bool hide();
    bool isEmpty() const;
    int length() const;
    int sorttype();

    const MusicMeta first();
    const MusicMeta prev(const MusicMeta &info);
    const MusicMeta next(const MusicMeta &info);
    const MusicMeta music(int index);
    const MusicMeta music(const QString &id);

    bool isLast(const MusicMeta &info);
    bool contains(const MusicMeta &info);
    MusicMetaList allmusic();
    void reset(const MusicMetaList &);

public slots:
    void setDisplayName(const QString &name);
    void appendMusic(const MusicMetaList &metalist);
    void removeMusic(const MusicMetaList &metalist);
    void removeOneMusic(const MusicMeta &meta);
    void sortBy(Playlist::SortType sortType);
    void resort();

    //! private interface
public:
    void load();

signals:
    void musiclistAdded(const MusicMetaList &metalist);
    void musicAdded(const MusicMeta &info);
    void musicRemoved(const MusicMeta &info);
    void removed();
    void displayNameChanged(QString displayName);

private:
    PlaylistMeta   listmeta;
};

typedef QSharedPointer<Playlist> PlaylistPtr;

extern const QString AllMusicListID;
extern const QString FavMusicListID;
extern const QString SearchMusicListID;

#endif // PLAYLIST_H
