import QtQuick
import QtQuick.Window

/**
 * The development consent gate: what is about to leave the machine, and a choice.
 *
 * Compiled in only when DEV_SEND_CONFIRM is set, which the presets do and a release build
 * does not (PHASE1.md section 6). The words shown are the payload exactly as FilterCore
 * produced it; masking happens inside the request builder, on top of this.
 */
Window {
    id: confirm

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 320

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 34 + 2 * shadowMargin

    property var words: []

    function ask(list) {
        words = list;
        // Centred on whichever screen the app is on: this is a development gate, and it has
        // to be impossible to miss.
        confirm.x = Math.round(Screen.virtualX + (Screen.width - width) / 2);
        confirm.y = Math.round(Screen.virtualY + (Screen.height - height) / 2);
        visible = true;
        requestActivate();
    }

    ShadowCard {
        anchors.fill: parent
        movable: true

        Column {
            id: column
            x: 20
            y: 18
            width: confirm.cardWidth - 40
            spacing: 0

            Text {
                text: qsTr("Send to the model?")
                color: Tokens.text
                font.pixelSize: 14
                font.weight: Font.Bold
            }

            Text {
                topPadding: 10
                width: parent.width
                text: confirm.words.join("\n")
                color: Tokens.text
                font.family: Tokens.monoFamily
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            Text {
                topPadding: 10
                width: parent.width
                text: qsTr("Development build: every request is shown before it is sent.")
                color: Tokens.faint
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }

            Item { width: 1; height: 14 }

            Row {
                width: parent.width
                spacing: 8
                layoutDirection: Qt.RightToLeft

                Rectangle {
                    width: sendLabel.width + 26
                    height: 27
                    radius: Tokens.radiusPill
                    color: Tokens.ink
                    Text {
                        id: sendLabel
                        anchors.centerIn: parent
                        text: qsTr("Send")
                        color: Tokens.on
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            confirm.visible = false;
                            Controller.confirmSend();
                        }
                    }
                }

                Rectangle {
                    width: cancelLabel.width + 26
                    height: 27
                    radius: Tokens.radiusPill
                    color: Tokens.panel2
                    border.width: 1
                    border.color: Tokens.line
                    Text {
                        id: cancelLabel
                        anchors.centerIn: parent
                        text: qsTr("Cancel")
                        color: Tokens.muted
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            confirm.visible = false;
                            Controller.cancelSend();
                        }
                    }
                }
            }
        }
    }
}
