pragma ComponentBehavior: Bound

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

    /// The open list's card in screen coordinates, or null when it is down. main.qml's
    /// outside-press rule reads it: a press that lands on the list belongs to the list, and the
    /// panel that opened it must not close over the reader's head.
    readonly property var openChildRect: list.visible
        ? Qt.rect(list.x + list.shadowMargin, list.y + list.shadowMargin,
                  list.width - 2 * list.shadowMargin, list.height - 2 * list.shadowMargin)
        : null

    /// @brief Take the list down. The panel calls this when it hides itself, since the list is
    ///        a window of its own and would otherwise outlive its owner.
    function closeList() {
        list.visible = false;
    }

    function openList() {
        // Where the field's bottom edge is, in screen coordinates.
        const point = button.mapToItem(null, 0, button.height + 6);
        const originX = Window.window ? Window.window.x : 0;
        const originY = Window.window ? Window.window.y : 0;
        // The list's window is shadowMargin bigger than its card on every side, so the window
        // has to be pulled back by that much for the card to land under the field. Placed
        // window-edge to field-edge it sat 26px right and 26px low.
        list.x = originX + point.x - list.shadowMargin;
        list.y = originY + point.y - list.shadowMargin;
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
            font.pixelSize: 13
        }

        Icon {
            anchors.right: parent.right
            anchors.rightMargin: 11
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            source: "qrc:/icons/ui-chevron-down.svg"
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
                        id: group
                        required property int index
                        required property var modelData

                        width: listColumn.width

                        // A rule between groups, never before the first one.
                        Rectangle {
                            visible: group.index > 0
                                     && group.modelData.group !== root.options[group.index - 1].group
                            width: parent.width
                            height: 1
                            color: Tokens.line2
                            anchors.margins: 5
                        }

                        Rectangle {
                            width: parent.width
                            height: root.rowHeight
                            radius: 8
                            color: group.modelData.value === root.currentValue ? Tokens.ink
                                                                               : (rowHover.hovered ? Tokens.panel2 : "transparent")
                            Behavior on color {
                                ColorAnimation {
                                    duration: Tokens.motion.press
                                    easing.type: Tokens.motion.easing
                                }
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 11
                                anchors.verticalCenter: parent.verticalCenter
                                text: group.modelData.label
                                color: group.modelData.value === root.currentValue ? Tokens.on : Tokens.text
                                font.pixelSize: 13
                            }

                            Text {
                                visible: group.modelData.note !== ""
                                anchors.right: parent.right
                                anchors.rightMargin: 11
                                anchors.verticalCenter: parent.verticalCenter
                                text: group.modelData.note
                                color: group.modelData.value === root.currentValue ? Qt.rgba(1, 1, 1, 0.62) : Tokens.faint
                                font.pixelSize: 11
                            }

                            HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: {
                                    root.picked(group.modelData.value);
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
