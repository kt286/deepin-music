// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.11
import QtQml 2.15
import QtQuick.Layouts 1.11
import QtQuick.Window 2.11
import QtQuick.Controls 2.4
import org.deepin.dtk 1.0

DialogWindow {
    id: lyricSearchDialog

    property string searchKeyword: ""
    property var searchResults: []
    property int selectedIndex: -1
    property string previewLyric: ""
    property bool searching: false
    property bool simplifyWordLyrics: true

    function convertToSimpleLyric(lyric) {
        if (lyric.length === 0) return ""
        var lines = lyric.split('\n')
        var result = []
        for (var i = 0; i < lines.length; i++) {
            var line = lines[i]
            var match = line.match(/^\[(\d{2}:\d{2}\.\d{2,3})\](.*)$/)
            if (match) {
                var timestamp = match[1]
                var content = match[2]
                var wordMatches = content.match(/\[(\d{2}:\d{2}\.\d{2,3})\][^\[]+/g)
                if (wordMatches && wordMatches.length > 0) {
                    var text = ''
                    for (var j = 0; j < wordMatches.length; j++) {
                        var wordMatch = wordMatches[j].match(/\[\d{2}:\d{2}\.\d{2,3}\](.*)$/)
                        if (wordMatch) {
                            text += wordMatch[1]
                        }
                    }
                    result.push('[' + timestamp + ']' + text)
                } else {
                    result.push(line)
                }
            } else {
                result.push(line)
            }
        }
        return result.join('\n')
    }

    signal lyricApplied()

    width: 720
    height: 600
    minimumWidth: 720
    minimumHeight: 600
    maximumWidth: 720
    maximumHeight: 600
    modality: Qt.ApplicationModal
    icon: globalVariant.appIconName
    Binding on flags {
        when: Qt.platform.os === "windows"
        value: Qt.Dialog | Qt.WindowCloseButtonHint | Qt.MSWindowsFixedSizeDialogHint | Qt.FramelessWindowHint
    }

    header: DialogTitleBar {
        enableInWindowBlendBlur: false
        content: Loader {
            sourceComponent: Label {
                property Palette textColor: Palette {
                    normal: Qt.rgba(0, 0, 0, 1)
                    normalDark: Qt.rgba(247.0 / 255.0, 247.0 / 255.0, 247.0 / 255.0, 1)
                }
                anchors.centerIn: parent
                textFormat: Text.PlainText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font: DTK.fontManager.t7
                text: qsTr("Search Lyrics")
                color: ColorSelector.textColor
            }
        }
    }

    onVisibleChanged: {
        if (visible && Qt.platform.os === "windows") {
            x = (Screen.width - width) / 2
            y = (Screen.height - height) / 2
        }
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
        previewArea.text = qsTr("Loading...")
        searchTimer.start()
    }

    function loadPreview(index) {
        if (index < 0 || index >= searchResults.length) {
            previewLyric = ""
            previewArea.text = qsTr("Select a result to preview lyrics")
            return
        }
        selectedIndex = index
        previewArea.text = qsTr("Loading...")
        previewTimer.start()
    }

    Item {
        width: parent.width > 0 ? parent.width : 720
        height: parent.height > 0 ? parent.height : 540

        Component.onCompleted: {
            width = Qt.binding(function() { return parent.width })
            height = Qt.binding(function() { return parent.height })
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
                } else {
                    previewArea.text = qsTr("No lyrics found")
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
                previewArea.text = lyric.length > 0 ? (simplifyWordLyrics ? convertToSimpleLyric(lyric) : lyric) : qsTr("No lyrics found")
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

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
                    enabled: !searching
                    onClicked: doSearch()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    border.color: palette.mid
                    border.width: 1
                    radius: 8
                    color: palette.base

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 4

                        RowLayout {
                            Layout.leftMargin: 8
                            Layout.topMargin: 4
                            Layout.bottomMargin: 4
                            Layout.preferredHeight: 24
                            spacing: 8

                            Label {
                                text: qsTr("Search Results") + " (%1)".arg(searchResults.length)
                                font: DTK.fontManager.t6
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Item { Layout.fillWidth: true }
                        }

                        ListView {
                            id: resultsList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.leftMargin: 8
                            Layout.rightMargin: 8
                            Layout.bottomMargin: 8
                            clip: true
                            model: searchResults
                            currentIndex: -1

                            BusyIndicator {
                                anchors.centerIn: parent
                                width: 48
                                height: 48
                                running: searching
                                visible: searching
                                z: 10
                            }

                            delegate: Rectangle {
                                width: resultsList.width
                                height: 52
                                radius: 8
                                color: resultsList.currentIndex === index ? palette.highlight : "transparent"

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 2

                                    Row {
                                        width: parent.width
                                        spacing: 8

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
                                            radius: 8
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

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    border.color: palette.mid
                    border.width: 1
                    radius: 8
                    color: palette.base

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 4

                        RowLayout {
                            Layout.leftMargin: 8
                            Layout.topMargin: 4
                            Layout.bottomMargin: 4
                            Layout.preferredHeight: 24
                            spacing: 8

                            Label {
                                text: qsTr("Lyrics Preview")
                                font: DTK.fontManager.t6
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Item { Layout.fillWidth: true }

                            Switch {
                                id: simplifySwitch
                                hoverEnabled: true
                                checked: simplifyWordLyrics
                                onCheckedChanged: {
                                    simplifyWordLyrics = checked
                                    if (previewLyric.length > 0) {
                                        previewArea.text = checked ? convertToSimpleLyric(previewLyric) : previewLyric
                                    }
                                }

                                ToolTip {
                                    visible: simplifySwitch.hovered
                                    text: qsTr("Simplify word-by-word lyrics")
                                }
                            }
                        }

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.leftMargin: 8
                            Layout.rightMargin: 8
                            Layout.bottomMargin: 8
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

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item { Layout.fillWidth: true }

                Button {
                    Layout.preferredWidth: 180
                    height: 30
                    text: qsTr("Cancel")
                    onClicked: lyricSearchDialog.close()
                }

                Button {
                    Layout.preferredWidth: 180
                    height: 30
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
}
