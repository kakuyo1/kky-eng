import QtQuick

/**
 * The outlined two-or-three-way switch UI.md 4.4 uses for language, theme and the words
 * filter: the selected item takes the ink fill, the rest stay plain.
 *
 * The background is a sibling of the Row, not a child of it: a Row lays its children out in a
 * line, and an item that fills the parent is not something it can place.
 */
Item {
    id: root

    property var labels: []
    property int currentIndex: 0

    signal picked(int index)

    Rectangle {
        anchors.fill: parent
        radius: Tokens.radiusField
        color: Tokens.panel2
        border.width: 1
        border.color: Tokens.line
    }

    Row {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        Repeater {
            model: root.labels

            delegate: Rectangle {
                required property int index
                required property var modelData

                width: (root.width - 2) / root.labels.length
                height: root.height - 2
                radius: Tokens.radiusField - 1
                color: index === root.currentIndex ? Tokens.ink : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: modelData
                    color: index === root.currentIndex ? Tokens.on : Tokens.muted
                    font.pixelSize: 12
                    font.weight: index === root.currentIndex ? Font.Bold : Font.Normal
                }

                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.picked(index) }
            }
        }
    }
}
