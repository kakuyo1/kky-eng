import QtQuick

/**
 * A slider over whole-number stops, drawn in the switch's colours. Dragging or tapping snaps to the nearest stop,
 * because a fractional magnification would break the square pixels of the pet's sprites. A non-interactive slider
 * is dimmed and does not respond. The owner binds `value` and takes the new stop from `moved`.
 */
Item {
    id: root

    property int from: 1
    property int to: 4
    property int value: from
    property bool interactive: true

    readonly property real knobSize: 16
    readonly property real travel: width - knobSize
    readonly property real ratio: to > from ? (value - from) / (to - from) : 0

    signal moved(int value)

    implicitWidth: 160
    implicitHeight: 20
    opacity: interactive ? 1.0 : 0.45

    function stopAt(x) {
        const clamped = Math.max(0, Math.min(1, (x - knobSize / 2) / travel));
        return from + Math.round(clamped * (to - from));
    }

    Rectangle {
        x: root.knobSize / 2
        width: root.travel
        height: 4
        anchors.verticalCenter: parent.verticalCenter
        radius: 2
        color: Tokens.line
    }

    Rectangle {
        x: root.knobSize / 2
        width: root.travel * root.ratio
        height: 4
        anchors.verticalCenter: parent.verticalCenter
        radius: 2
        color: Tokens.ink
    }

    Rectangle {
        width: root.knobSize
        height: root.knobSize
        radius: root.knobSize / 2
        y: (root.height - height) / 2
        x: root.travel * root.ratio
        color: Tokens.ink
        border.width: 2
        border.color: Tokens.panel
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.interactive
        cursorShape: root.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor

        onPressed: (mouse) => root.moveTo(mouse.x)
        onPositionChanged: (mouse) => {
            if (pressed)
                root.moveTo(mouse.x);
        }
    }

    function moveTo(x) {
        const stop = stopAt(x);
        if (stop !== value)
            moved(stop);
    }
}
