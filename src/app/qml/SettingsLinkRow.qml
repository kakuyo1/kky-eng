import QtQuick

Item {
    id: root

    property string label: ""
    property string note: ""
    signal picked()

    width: parent ? parent.width : 0
    height: 44

    Rectangle {
        anchors.fill: parent
        color: hover.hovered ? Tokens.panel2 : "transparent"
        radius: Tokens.radiusField
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        color: Tokens.text
        font.pixelSize: 13
    }

    Text {
        anchors.right: arrow.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: root.note
        color: Tokens.faint
        font.pixelSize: 11
        visible: root.note !== ""
    }

    Text {
        id: arrow
        anchors.right: parent.right
        anchors.rightMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        text: "›"
        color: Tokens.faint
        font.pixelSize: 20
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler { onTapped: root.picked() }
}
