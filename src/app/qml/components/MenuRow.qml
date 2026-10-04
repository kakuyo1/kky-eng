import QtQuick

/**
 * One row of the tray menu (UI.md section 4.2): a glyph and a label, an optional value on the
 * right in the monospace face, and the hover fill. The dangerous one -- Quit -- is the same row
 * in the danger colour.
 */
Item {
    id: root

    property string source: ""
    property string label: ""
    property string note: ""
    property bool danger: false

    /// Whether the pointer is on this row. The language row watches it to unfold its list.
    readonly property alias hovered: hover.hovered

    signal picked()

    implicitHeight: 32

    Rectangle {
        anchors.fill: parent
        radius: Tokens.radiusField
        color: hover.hovered ? Tokens.panel2 : "transparent"
        Behavior on color {
            ColorAnimation {
                duration: Tokens.motion.press
                easing.type: Tokens.motion.easing
            }
        }
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler { onTapped: root.picked() }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 11
        anchors.verticalCenter: parent.verticalCenter
        spacing: 9

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            source: root.source
            color: root.danger ? Tokens.danger : Tokens.muted
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: root.danger ? Tokens.danger : Tokens.text
            font.pixelSize: 13
        }
    }

    MixedText {
        visible: root.note !== ""
        id: noteText
        anchors.right: parent.right
        anchors.rightMargin: 11
        anchors.verticalCenter: parent.verticalCenter
        value: root.note
        color: Tokens.faint
        pixelSize: 11
    }
}
