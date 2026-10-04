import QtQuick

/**
 * One labelled figure in the statistics and cost panels (UI.md 4.6 / 4.8).
 *
 * A clickable row widens its hover area past the text and grows a chevron, which is how the
 * two drill-downs announce themselves.
 *
 * `note` is a second, quieter figure on the same line, left of `value`: the cost rows put the
 * tokens their amount was bought with there. It is its own slot rather than more words in
 * `value` because the two are weighted differently -- the amount is the row's subject and keeps
 * the bold right-aligned face, the count is context -- and because one string wide enough to
 * hold both runs into the label on a card this narrow.
 */
Item {
    id: root

    property string label: ""
    property string value: ""
    property string note: ""
    property bool clickable: false

    signal tapped()

    height: 29

    Rectangle {
        // The hover fill reaches 8px past the text on both sides, so the target is bigger
        // than the words in it.
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.clickable ? -8 : 0
        anchors.rightMargin: root.clickable ? -8 : 0
        height: parent.height - 2
        radius: 8
        color: root.clickable && rowHover.hovered ? Tokens.panel2 : "transparent"
        Behavior on color {
            ColorAnimation {
                duration: Tokens.motion.press
                easing.type: Tokens.motion.easing
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        color: Tokens.muted
        font.pixelSize: 13
    }

    MixedText {
        id: noteText
        visible: root.note !== ""
        anchors.right: valueText.left
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        value: root.note
        color: Tokens.faint
        pixelSize: 11
    }

    MixedText {
        id: valueText
        anchors.right: chevron.left
        anchors.rightMargin: root.clickable ? 6 : 0
        anchors.verticalCenter: parent.verticalCenter
        value: root.value
        color: Tokens.text
        pixelSize: 13
        weight: Font.Bold
    }

    Icon {
        id: chevron
        visible: root.clickable
        anchors.right: parent.right
        anchors.rightMargin: root.clickable ? -8 : 0
        anchors.verticalCenter: parent.verticalCenter
        width: 13
        height: 13
        source: "qrc:/icons/ui-chevron-right.svg"
        color: Tokens.faint
    }

    HoverHandler { id: rowHover; cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor }
    TapHandler { enabled: root.clickable; onTapped: root.tapped() }
}
