import QtQuick

/**
 * One labelled figure in the statistics and cost panels (UI.md 4.6 / 4.8).
 *
 * A clickable row widens its hover area past the text and grows a chevron, which is how the
 * two drill-downs announce themselves.
 */
Item {
    id: root

    property string label: ""
    property string value: ""
    property bool clickable: false

    signal tapped()

    height: 29

    Rectangle {
        // The hover fill reaches 8px past the text on both sides, so the target is bigger
        // than the words in it.
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: clickable ? -8 : 0
        anchors.rightMargin: clickable ? -8 : 0
        height: parent.height - 2
        radius: 8
        color: clickable && rowHover.hovered ? Tokens.panel2 : "transparent"
    }

    Text {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        color: Tokens.muted
        font.family: Tokens.fontFamily
        font.pixelSize: 13
    }

    Text {
        id: valueText
        anchors.right: chevron.left
        anchors.rightMargin: root.clickable ? 6 : 0
        anchors.verticalCenter: parent.verticalCenter
        text: root.value
        color: Tokens.text
        font.family: Tokens.monoFamily
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }

    Icon {
        id: chevron
        visible: root.clickable
        anchors.right: parent.right
        anchors.rightMargin: root.clickable ? -8 : 0
        anchors.verticalCenter: parent.verticalCenter
        width: 13
        height: 13
        path: "M9 6l6 6-6 6"
        strokeWidth: 1.8
        color: Tokens.faint
    }

    HoverHandler { id: rowHover; cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor }
    TapHandler { enabled: root.clickable; onTapped: root.tapped() }
}
