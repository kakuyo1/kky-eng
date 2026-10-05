import QtQuick

Item {
    id: root

    property string title: ""
    property string summary: ""
    signal picked()

    width: parent ? parent.width : 0
    height: 60

    Rectangle {
        anchors.fill: parent
        radius: 9
        color: hover.hovered ? Tokens.panel2 : "transparent"
    }

    Column {
        anchors.left: parent.left
        anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3

        Text {
            text: root.title
            color: Tokens.text
            font.pixelSize: 13
        }
        Text {
            text: root.summary
            color: Tokens.muted
            font.pixelSize: 11
        }
    }

    Text {
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
