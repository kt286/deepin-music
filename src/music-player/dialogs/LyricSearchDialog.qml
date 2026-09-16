// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.11
import QtQuick.Layouts 1.11
import QtQuick.Controls 2.4
import QtQuick.Window 2.2
import org.deepin.dtk 1.0

Window {
    id: lyricSearchDialog

    property string searchKeyword: ""
    property var searchResults: []
    property int selectedIndex: -1
    property string previewLyric: ""
    property bool searching: false

    signal lyricApplied()

    title: qsTr("Search Lyrics")
    width: 720
    height: 520
    modality: Qt.ApplicationModal
    flags: Qt.Dialog | Qt.WindowCloseButtonHint

    onVisibleChanged: {
        if (visible) {
            searchInput.text = searchKeyword
            if (searchKeyword.length > 0) {
                doSearch()
            }
        }
    }

    onSearchKeywordChanged: {
        if (visible) {
            searchInput.text = searchKeyword
        }
    }

    function doSearch() {
        if (searchKeyword.length === 0) return
        searching = true
        searchResults = []
        selectedIndex = -1
        previewLyric = ""
        searchTimer.start()
    }

    function loadPreview(index) {
        if (index < 0 || index >= searchResults.length) {
            previewLyric = ""
            return
        }
        selectedIndex = index
        previewArea.text = qsTr("Loading...")
        previewTimer.start()
    }

    Timer {
        id: searchTimer
        interval: 50
        onTriggered: {
            searchResults = Presenter.searchLyrics(searchKeyword)
            searching = false
            if (searchResults.length > 0) {
                resultsList.currentIndex = 0
                loadPreview(0)
            }
        }
    }

    Timer {
        id: previewTimer
        interval: 50
        onTriggered: {
            if (lyricSearchDialog.selectedIndex < 0) return
            var result = searchResults[lyricSearchDialog.selectedIndex]
            var lyric = ""
            if (result.source === "NetEase") {
                lyric = Presenter.getLyricsFromNetEase(result.id)
            } else if (result.source === "Kugou") {
                lyric = Presenter.getLyricsFromKugou(result.kugouHash, result.id, result.title, result.artist, result.album, result.duration)
            } else if (result.source === "LRCLIB") {
                lyric = Presenter.getLyricsFromLrclib(result.lrclibTrackName, result.lrclibArtistName, result.lrclibAlbumName, result.duration)
            }
            previewLyric = lyric
            previewArea.text = lyric.length > 0 ? lyric : qsTr("No lyrics found")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        // Search bar
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            LineEdit {
                id: searchInput
                Layout.fillWidth: true
                placeholderText: qsTr("Enter song name and artist")
                text: searchKeyword
                onAccepted: doSearch()
            }

            Button {
                text: qsTr("Search")
                onClicked: doSearch()
            }
        }

        // Main content: results list + preview
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // Results list
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                border.color: palette.mid
                border.width: 1
                radius: 6

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 4

                    Label {
                        text: qsTr("Search Results") + " (%1)".arg(searchResults.length)
                        font: DTK.fontManager.t6
                        Layout.leftMargin: 8
                    }

                    ListView {
                        id: resultsList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: searchResults
                        currentIndex: -1

                        delegate: Rectangle {
                            width: resultsList.width
                            height: 52
                            radius: 4
                            color: resultsList.currentIndex === index ? palette.highlight : "transparent"

                            Column {
                                anchors.fill: parent
                                anchors.margins: 6
                                spacing: 2

                                Row {
                                    width: parent.width
                                    spacing: 6

                                    Label {
                                        text: modelData.title
                                        font: DTK.fontManager.t7
                                        elide: Text.ElideRight
                                        width: parent.width - sourceLabel.width - 8
                                        color: resultsList.currentIndex === index ? palette.highlightedText : palette.text
                                    }

                                    Rectangle {
                                        id: sourceLabel
                                        width: sourceText.width + 12
                                        height: 18
                                        radius: 9
                                        color: modelData.source === "Kugou" ? "#1a73e8" : modelData.source === "NetEase" ? "#e8192c" : "#e8710a"
                                        anchors.verticalCenter: parent.verticalCenter

                                        Label {
                                            id: sourceText
                                            anchors.centerIn: parent
                                            text: modelData.source
                                            font.pixelSize: 10
                                            color: "white"
                                        }
                                    }
                                }

                                Label {
                                    text: modelData.artist + (modelData.album.length > 0 ? " - " + modelData.album : "")
                                    font: DTK.fontManager.t9
                                    elide: Text.ElideRight
                                    width: parent.width
                                    color: resultsList.currentIndex === index ? palette.highlightedText : palette.placeholderText
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    resultsList.currentIndex = index
                                    loadPreview(index)
                                }
                            }
                        }
                    }
                }
            }

            // Preview area
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                border.color: palette.mid
                border.width: 1
                radius: 6

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 4

                    Label {
                        text: qsTr("Lyrics Preview")
                        font: DTK.fontManager.t6
                        Layout.leftMargin: 8
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        TextArea {
                            id: previewArea
                            readOnly: true
                            wrapMode: TextArea.Wrap
                            text: qsTr("Select a result to preview lyrics")
                            font.family: "monospace"
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }

        // Bottom buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Cancel")
                onClicked: lyricSearchDialog.close()
            }

            Button {
                text: qsTr("Apply")
                enabled: previewLyric.length > 0
                onClicked: {
                    if (Presenter.applyLyric(previewLyric)) {
                        lyricSearchDialog.close()
                        lyricApplied()
                    }
                }
            }
        }
    }
}
