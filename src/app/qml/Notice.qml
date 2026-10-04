import QtQuick
import QtQuick.Window

/**
 * A reason card for work that could not produce an explanation.
 *
 * A notice has no selection anchor: it is a separate surface and centers itself on the current
 * screen. Its close path only clears notice state, so a stale bubble or action cannot dismiss a
 * newer surface.
 */
Window {
    id: notice

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 320

    property string noticeTitle: ""
    property string noticeBody: ""
    property string noticeKind: "info"

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 36 + 2 * shadowMargin

    function show(payload) {
        noticeTitle = payload.title || ""
        noticeBody = payload.body || ""
        noticeKind = payload.kind || "info"
        x = Math.round(Screen.virtualX + (Screen.width - width) / 2)
        y = Math.round(Screen.virtualY + (Screen.height - height) / 2)
        visible = true
        raise()
    }

    function closeNotice() {
        visible = false
        Controller.dismissNotice()
    }

    ShadowCard {
        anchors.fill: parent

        Column {
            id: column
            x: 20
            y: 18
            width: notice.cardWidth - 40
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
                    text: notice.noticeTitle
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
                    TapHandler { onTapped: notice.closeNotice() }
                }
            }

            Text {
                width: parent.width
                text: notice.noticeBody
                color: notice.noticeKind === "error" ? Tokens.danger : Tokens.text
                font.pixelSize: 13
                lineHeight: 1.35
                lineHeightMode: Text.ProportionalHeight
                wrapMode: Text.WordWrap
            }
        }
    }
}
