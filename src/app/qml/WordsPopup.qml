pragma ComponentBehavior: Bound

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

    /// The reader asked for the panel this one was opened from.
    signal backRequested()

    ShadowCard {
        anchors.fill: parent

        Column {
            id: column
            x: 20
            y: 18
            width: words.cardWidth - 40
            spacing: 0

            // The icons are anchored to the right edge rather than pushed there by a spacer:
            // a spacer sized around the English title lands the glyphs past the card the
            // moment the title is a different width, which is every translation of it.
            Item {
                width: parent.width
                height: title.height

                Text {
                    id: title
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Words")
                    color: Tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }

                // The title row is the handle rather than the card, because the card holds the
                // list: a drag inside a list is a scroll, and a handler covering it would be
                // competing with the flick for the same gesture. The timer below does the
                // moving, and the two reasons it is not the handler's own signal are in
                // ShadowCard.qml.
                DragHandler {
                    id: mover
                    target: null

                    property point grabCursor: Qt.point(0, 0)
                    property point grabWindow: Qt.point(0, 0)

                    function place() {
                        const at = Controller.cursorPos();
                        words.x = Math.round(grabWindow.x + at.x - grabCursor.x);
                        words.y = Math.round(grabWindow.y + at.y - grabCursor.y);
                    }

                    onActiveChanged: {
                        if (active) {
                            grabCursor = Controller.cursorPos();
                            grabWindow = Qt.point(words.x, words.y);
                        } else {
                            place();
                        }
                    }
                }

                Timer {
                    interval: 16
                    repeat: true
                    running: mover.active
                    onTriggered: mover.place()
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    // Back to the statistics panel, which is where this one opens from. This
                    // panel replaces it rather than stacking on it, so the way up has to be
                    // visible.
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/ui-back.svg"
                        color: Tokens.faint
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: words.backRequested() }
                    }
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/ui-close.svg"
                        color: Tokens.faint
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: words.visible = false }
                    }
                }
            }

            Text {
                topPadding: 3
                // The list's own count, not the all-time tally the statistics panel shows:
                // the rows below are deduplicated, so a word explained twice counts once here
                // and twice there. A number that disagrees with the list under it reads as a
                // bug, whichever of the two meanings it was meant to carry.
                text: qsTr("%1 words").arg(Controller.words.length)
                color: Tokens.faint
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
                // UI.md 4.7 gives the list a *maximum* of 306px: a handful of words leaves a
                // panel sized to those words, not a panel with a hole under them.
                height: Math.min(306, list.contentHeight)

                ListView {
                    id: list
                    width: parent.width
                    height: parent.height
                    clip: true
                    model: Controller.words
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Item {
                        id: entry
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
                            text: entry.modelData.word
                            color: Tokens.text
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }

                        Text {
                            anchors.right: tag.left
                            anchors.rightMargin: 9
                            anchors.verticalCenter: parent.verticalCenter
                            text: entry.modelData.when
                            color: Tokens.faint
                            font.family: Tokens.monoFamily
                            font.pixelSize: 11
                        }

                        Rectangle {
                            id: tag
                            visible: entry.modelData.status !== ""
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: tagLabel.width + 16
                            height: 17
                            radius: Tokens.radiusPill
                            color: Tokens.panel2
                            Text {
                                id: tagLabel
                                anchors.centerIn: parent
                                text: entry.modelData.status
                                color: Tokens.muted
                                font.pixelSize: 11
                                // The same pill the bubble shows over the same verdict, so
                                // it carries the same weight UI.md 3.2 gives a tag.
                                font.weight: Font.Bold
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
