import QtQuick
import QtQuick.Window

/**
 * Every word the bubble has shown (UI.md section 4.7), newest first, with a filter across the
 * top. The controller hands over finished strings: the relative time labels depend on the
 * current date, which is not something a view should be working out.
 */
Window {
    id: words

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 320

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 34 + 2 * shadowMargin

    /// 0 = all, 1 = known, 2 = new.
    property int filter: 0

    function openNear(anchor) {
        words.x = Math.max(8, anchor.x - width + 60);
        words.y = Math.max(8, anchor.y - height);
        visible = true;
    }

    ShadowCard {
        anchors.fill: parent

        Column {
            id: column
            x: 20
            y: 18
            width: words.cardWidth - 40
            spacing: 0

            Row {
                width: parent.width
                Text {
                    text: qsTr("Words")
                    color: Tokens.text
                    font.family: Tokens.fontFamily
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Item { width: parent.width - 40; height: 1 }
                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    path: "M5 5l14 14M19 5L5 19"
                    color: Tokens.faint
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: words.visible = false }
                }
            }

            Text {
                topPadding: 3
                text: qsTr("%1 words").arg(controller.stats.historyTotal)
                color: Tokens.faint
                font.family: Tokens.fontFamily
                font.pixelSize: 12
            }

            Item { width: 1; height: 12 }
            Segment {
                width: parent.width
                height: 28
                labels: [qsTr("All"), qsTr("Known"), qsTr("New")]
                currentIndex: words.filter
                onPicked: (index) => words.filter = index
            }

            Item { width: 1; height: 13 }
            Rectangle { width: parent.width; height: 1; color: Tokens.line2 }
            Item { width: 1; height: 6 }

            Item {
                width: parent.width
                height: 306

                ListView {
                    id: list
                    width: parent.width
                    height: parent.height
                    clip: true
                    model: controller.words
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Item {
                        required property var modelData

                        width: words.cardWidth - 40
                        height: visible ? 31 : 0
                        // The verdict is compared by key, not by the translated label: the
                        // two would drift apart the moment a translation changed.
                        visible: words.filter === 0
                                 || (words.filter === 1 && modelData.verdict === "known")
                                 || (words.filter === 2 && modelData.verdict === "new")

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 130
                            text: modelData.word
                            color: Tokens.text
                            font.family: Tokens.fontFamily
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }

                        Text {
                            anchors.right: tag.left
                            anchors.rightMargin: 9
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.when
                            color: Tokens.faint
                            font.family: Tokens.monoFamily
                            font.pixelSize: 11
                        }

                        Rectangle {
                            id: tag
                            visible: modelData.status !== ""
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: tagLabel.width + 16
                            height: 17
                            radius: Tokens.radiusPill
                            color: Tokens.panel2
                            Text {
                                id: tagLabel
                                anchors.centerIn: parent
                                text: modelData.status
                                color: Tokens.muted
                                font.family: Tokens.fontFamily
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                // The six-pixel scrollbar UI.md 4.7 asks for, drawn rather than imported so
                // the module needs no widget styling of its own.
                Rectangle {
                    visible: list.contentHeight > list.height
                    width: 6
                    radius: 3
                    color: Tokens.line
                    x: parent.width - 6
                    height: Math.max(24, list.height * list.height / list.contentHeight)
                    y: list.contentHeight > list.height
                       ? list.contentY / (list.contentHeight - list.height) * (list.height - height)
                       : 0
                }
            }
        }
    }
}
