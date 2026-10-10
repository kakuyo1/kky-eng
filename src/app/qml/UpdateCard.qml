import QtQuick
import QtQuick.Window

/**
 * The card a new version raises.
 *
 * A passive surface: it takes no part in the panels' mutual exclusion, and it never uses the
 * explanation notice slot, so a card standing can neither replace a notice nor be replaced by
 * one. Its close path clears only the card's own flag -- the version the reader skipped is a
 * separate decision the card's Skip button makes.
 *
 * Nothing is downloaded, installed or run from here. View opens the release page in the
 * browser and stops there.
 */
Window {
    id: card

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 340

    /// The version on offer, the release page to open, and the notes to read.
    property string version: ""
    property string pageUrl: ""
    property string notes: ""

    /// View was pressed: open the release page in the browser.
    signal viewRequested()
    /// Skip was pressed: stop asking about this exact version.
    signal skipRequested()

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 36 + 2 * shadowMargin

    /// Put the card in the middle of the current screen and raise it, the way Notice.qml does.
    function show(payload) {
        version = payload.version || ""
        pageUrl = payload.pageUrl || ""
        notes = payload.notes || ""
        x = Math.round(Screen.virtualX + (Screen.width - width) / 2)
        y = Math.round(Screen.virtualY + (Screen.height - height) / 2)
        visible = true
        raise()
    }

    /// Take the card down. It writes nothing: the cross says "not now", and Skip says "not this
    /// version", which are different answers to the same question.
    function closeCard() {
        visible = false
        Controller.closeUpdateCard()
    }

    ShadowCard {
        anchors.fill: parent

        Column {
            id: column
            x: 20
            y: 18
            width: card.cardWidth - 40
            spacing: 12

            Item {
                width: parent.width
                height: title.implicitHeight

                Text {
                    id: title
                    anchors.left: parent.left
                    anchors.right: close.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTranslate("UpdateCard", "Lens %1 is available").arg(card.version)
                    color: Tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }

                Icon {
                    id: close
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/icons/ui-close.svg"
                    color: Tokens.faint
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: card.closeCard() }
                }
            }

            // Empty rather than zero-height when there is nothing to read: a positioner skips an
            // invisible child whole, and an empty Text still opens a gap.
            Text {
                width: parent.width
                text: card.notes
                visible: text !== ""
                color: Tokens.faint
                font.pixelSize: 13
                lineHeight: 1.35
                lineHeightMode: Text.ProportionalHeight
                wrapMode: Text.WordWrap
            }

            Item {
                width: parent.width
                height: row.height

                Row {
                    id: row
                    width: viewButton.width + 12 + skipButton.width
                    height: 26
                    spacing: 12

                    Rectangle {
                        id: viewButton
                        width: viewLabel.width + 26
                        height: 26
                        radius: Tokens.radiusPill
                        color: Tokens.ink
                        scale: viewTap.pressed ? 0.97 : 1.0
                        Behavior on scale {
                            NumberAnimation {
                                duration: Tokens.motion.press
                                easing.type: Tokens.motion.easing
                            }
                        }

                        Text {
                            id: viewLabel
                            anchors.centerIn: parent
                            text: qsTranslate("UpdateCard", "View")
                            color: Tokens.on
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            id: viewTap
                            onTapped: {
                                card.visible = false
                                card.viewRequested()
                            }
                        }
                    }

                    Rectangle {
                        id: skipButton
                        width: skipLabel.width + 26
                        height: 26
                        radius: Tokens.radiusPill
                        color: "transparent"
                        border.width: 1
                        border.color: Tokens.line
                        scale: skipTap.pressed ? 0.97 : 1.0
                        Behavior on scale {
                            NumberAnimation {
                                duration: Tokens.motion.press
                                easing.type: Tokens.motion.easing
                            }
                        }

                        Text {
                            id: skipLabel
                            anchors.centerIn: parent
                            text: qsTranslate("UpdateCard", "Skip this version")
                            color: Tokens.text
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            id: skipTap
                            onTapped: {
                                card.visible = false
                                card.skipRequested()
                            }
                        }
                    }
                }
            }
        }
    }
}