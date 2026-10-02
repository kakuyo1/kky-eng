import QtQuick
import QtQuick.Window

/**
 * The collapsed dropdown from UI.md 4.4, and the grouped list it opens.
 *
 * The list is a window of its own rather than a child item: the settings panel is exactly as
 * tall as its contents, so a list drawn inside it would be clipped the moment it opened.
 */
Item {
    id: root

    /// The controller's level list: {value, label, group, note}.
    property var options: []
    property int currentValue: 0

    signal picked(int value)

    implicitHeight: 35

    readonly property var current: {
        for (let i = 0; i < options.length; ++i)
            if (options[i].value === currentValue)
                return options[i];
        return null;
    }

    readonly property int rowHeight: 29

    function openList() {
        const point = button.mapToItem(null, 0, button.height + 6);
        list.x = Window.window ? Window.window.x + point.x : point.x;
        list.y = Window.window ? Window.window.y + point.y : point.y;
        list.width = root.width + 2 * list.shadowMargin;
        list.visible = true;
    }

    Rectangle {
        id: button
        anchors.fill: parent
        radius: Tokens.radiusField
        color: Tokens.panel2
        border.width: 1
        border.color: Tokens.line

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 11
            anchors.verticalCenter: parent.verticalCenter
            text: root.current ? root.current.label : ""
            color: Tokens.text
            font.family: Tokens.fontFamily
            font.pixelSize: 13
        }

        Icon {
            anchors.right: parent.right
            anchors.rightMargin: 11
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            path: "M6 9l6 6 6-6"
            strokeWidth: 1.8
            color: Tokens.faint
        }

        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.openList() }
    }

    Window {
        id: list

        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        color: "transparent"
        visible: false

        readonly property int shadowMargin: 26

        height: listColumn.implicitHeight + 10 + 2 * shadowMargin

        ShadowCard {
            anchors.fill: parent
            radius: Tokens.radiusGroup

            Column {
                id: listColumn
                x: 5
                y: 5
                width: list.width - 2 * list.shadowMargin - 10

                Repeater {
                    model: root.options

                    delegate: Column {
                        required property int index
                        required property var modelData

                        width: listColumn.width

                        // A rule between groups, never before the first one.
                        Rectangle {
                            visible: index > 0 && modelData.group !== root.options[index - 1].group
                            width: parent.width
                            height: 1
                            color: Tokens.line2
                            anchors.margins: 5
                        }

                        Rectangle {
                            width: parent.width
                            height: root.rowHeight
                            radius: 8
                            color: modelData.value === root.currentValue ? Tokens.ink
                                                                         : (rowHover.hovered ? Tokens.panel2 : "transparent")

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 11
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                color: modelData.value === root.currentValue ? Tokens.on : Tokens.text
                                font.family: Tokens.fontFamily
                                font.pixelSize: 13
                            }

                            Text {
                                visible: modelData.note !== ""
                                anchors.right: parent.right
                                anchors.rightMargin: 11
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.note
                                color: modelData.value === root.currentValue ? Qt.rgba(1, 1, 1, 0.62) : Tokens.faint
                                font.family: Tokens.fontFamily
                                font.pixelSize: 11
                            }

                            HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: {
                                    root.picked(modelData.value);
                                    list.visible = false;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
