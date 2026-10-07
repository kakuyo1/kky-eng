pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The collapsed dropdown from UI.md 4.4, and the grouped list it opens.
 *
 * The list is a window of its own rather than a child item: the settings panel is exactly as
 * tall as its contents, so a list drawn inside it would be clipped the moment it opened.
 *
 * Two shapes share the one component because they share the list. A field the reader only picks
 * from shows the current option's label, and the whole field opens the list. An `editable` one
 * shows what was typed instead, and there only the chevron opens the list -- the rest of the
 * field is the reader's cursor, which is what makes a model name the service never listed
 * reachable by hand (UI.md 4.4, docs/adr/0017).
 *
 * A chevron with no list behind it is dimmed and takes no press, the way a switch that cannot be
 * moved is: a service that never answered has no models to offer, and the field is still the
 * reader's to type in. It is greyed rather than taken away because the shape of this control is
 * the thing the reader reads as "the setting took" -- one that comes and goes is worse than one
 * that says nothing is there (docs/adr/0017).
 */
Item {
    id: root

    /// The controller's level list: {value, label, group, note}.
    property var options: []
    property var currentValue: 0

    /// Whether the value can be typed as well as picked.
    property bool editable: false

    signal picked(var value)
    /// A value was typed and the reader left the field. Only ever emitted when `editable`.
    signal edited(string text)

    implicitHeight: 35

    readonly property var current: {
        for (let i = 0; i < options.length; ++i)
            if (options[i].value === currentValue)
                return options[i];
        return null;
    }

    readonly property int rowHeight: 29

    /// Whether there is a list to open at all.
    readonly property bool hasOptions: options.length > 0

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
        list.visible = true;
    }

    Rectangle {
        id: button
        anchors.fill: parent
        radius: Tokens.radiusField
        color: Tokens.panel2
        border.width: 1
        border.color: root.editable && input.activeFocus ? Tokens.ink : Tokens.line

        Text {
            id: label
            visible: !root.editable
            anchors.left: parent.left
            anchors.leftMargin: 11
            anchors.right: chevron.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.current ? root.current.label : ""
            color: Tokens.text
            font.pixelSize: 13
            elide: Text.ElideRight
        }

        // The typed value. Its binding is destroyed by the first keystroke, as TextInput always
        // does, and put back once the reader leaves the field so the controller's answer -- which
        // trims -- is what the field goes on showing.
        TextInput {
            id: input
            objectName: "fieldInput"
            visible: root.editable
            anchors.left: parent.left
            anchors.leftMargin: 11
            anchors.right: chevron.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            verticalAlignment: TextInput.AlignVCenter
            text: root.currentValue
            color: Tokens.text
            font.pixelSize: 13
            selectByMouse: true
            onEditingFinished: {
                if (text !== root.currentValue)
                    root.edited(text);
                text = Qt.binding(() => root.currentValue);
            }
        }

        Icon {
            id: chevron
            anchors.right: parent.right
            anchors.rightMargin: 11
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            source: "qrc:/icons/ui-chevron-down.svg"
            color: Tokens.faint
            // Dimmed on the same rule Switch dims by, and for the same reason: there is nothing
            // behind it to open.
            opacity: root.hasOptions ? 1.0 : 0.45
        }

        // Only the chevron is a target once the field is editable; the rest of it is where the
        // reader types. Wide enough to be hit without aiming: a 14px glyph is not.
        Item {
            visible: root.editable
            anchors.right: chevron.right
            anchors.rightMargin: -8
            anchors.verticalCenter: parent.verticalCenter
            width: 30
            height: parent.height - 4

            HoverHandler { cursorShape: root.hasOptions ? Qt.PointingHandCursor : Qt.ArrowCursor }
            TapHandler {
                enabled: root.hasOptions
                onTapped: root.openList()
            }
        }

        HoverHandler {
            enabled: !root.editable
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            enabled: !root.editable
            onTapped: root.openList()
        }
    }

    Window {
        id: list

        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        color: "transparent"
        visible: false

        readonly property int shadowMargin: 26
        /// Room the rows leave inside the card, on every side.
        readonly property int listPadding: 10
        /// How tall the rows may get before they scroll. Ten of them: a service that resells
        /// other people's models answers with several hundred ids, and the window has to stay on
        /// the screen with room for the shadow around it.
        readonly property int maximumListHeight: 290

        width: root.width + 2 * shadowMargin
        height: listColumn.height + 2 * listPadding + 2 * shadowMargin

        ShadowCard {
            anchors.fill: parent
            radius: Tokens.radiusGroup

            // A view rather than a column: the delegates are built as they come into sight, so a
            // list of several hundred costs the ten that are drawn and no more.
            //
            // Placed by hand rather than by anchors: `ShadowCard` hands its children to the card
            // itself, so this item's origin is the card -- not the window, which is a shadow
            // margin wider on every side -- and the insets below are measured from the card.
            // Counting the margin in as well put every row 26px right and 26px low of the field
            // it belongs to.
            ListView {
                id: listColumn
                x: list.listPadding
                y: list.listPadding
                width: list.width - 2 * (list.shadowMargin + list.listPadding)
                height: Math.min(contentHeight, list.maximumListHeight)
                clip: true
                model: root.options
                boundsBehavior: Flickable.StopAtBounds

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

            // The scrollbar, drawn rather than imported so the module needs no widget styling of
            // its own -- the same six pixels UI.md 4.8 asks of the word list. Without it a list of
            // three hundred looks like a list of ten.
            Rectangle {
                visible: listColumn.contentHeight > listColumn.height
                width: 6
                radius: 3
                color: Tokens.line
                x: listColumn.x + listColumn.width + 2
                height: Math.max(24, listColumn.height * listColumn.height / listColumn.contentHeight)
                y: listColumn.y + (listColumn.contentHeight > listColumn.height
                                   ? listColumn.contentY / (listColumn.contentHeight - listColumn.height) * (listColumn.height - height)
                                   : 0)
            }
        }
    }
}
