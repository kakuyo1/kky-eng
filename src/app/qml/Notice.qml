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

    /// The one thing the reader can do about this notice, empty when there is nothing to do. A
    /// notice is normally the end of the road -- a request failed, and there is nothing left to
    /// press -- but a word with no stored explanation has an obvious next step, and the reader who
    /// asked for it by pointing at it should not have to go and find it somewhere else.
    property string noticeAction: ""
    /// The word that action acts on. The surface does not know what the action means; the one that
    /// raised the notice does, and it reads this back off the controller.
    property string noticeLemma: ""

    /// The action was pressed.
    signal actionTriggered()

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 36 + 2 * shadowMargin

    function show(payload) {
        noticeTitle = payload.title || ""
        noticeBody = payload.body || ""
        noticeKind = payload.kind || "info"
        noticeAction = payload.action || ""
        noticeLemma = payload.lemma || ""
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

            // Invisible rather than zero-height when there is nothing to press: a positioner skips
            // an invisible child whole, spacing included, and a zero-height one would still open a
            // gap between the reason and the card's floor.
            Item {
                visible: notice.noticeAction !== ""
                width: parent.width
                height: visible ? action.height : 0

                Rectangle {
                    id: action
                    width: actionLabel.width + 26
                    height: actionLabel.height + 12
                    radius: Tokens.radiusPill
                    color: Tokens.ink
                    scale: actionTap.pressed ? 0.97 : 1.0
                    Behavior on scale {
                        NumberAnimation {
                            duration: Tokens.motion.press
                            easing.type: Tokens.motion.easing
                        }
                    }

                    Text {
                        id: actionLabel
                        anchors.centerIn: parent
                        text: notice.noticeAction
                        color: Tokens.on
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }

                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        id: actionTap
                        onTapped: notice.actionTriggered()
                    }
                }
            }
        }
    }
}
