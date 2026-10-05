import QtQuick

/**
 * The 34x20 pill switch from UI.md 4.4.
 *
 * A disabled switch is dimmed and does not respond. It carries no explanation on the surface:
 * the phase-1 placeholders are meant to read as unavailable, not as a promise.
 */
Item {
    id: root

    property bool checked: false
    property bool interactive: true

    signal toggled(bool checked)

    implicitWidth: 34
    implicitHeight: 20
    opacity: interactive ? 1.0 : 0.45

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? Tokens.ink : Tokens.line
        Behavior on color {
            ColorAnimation {
                duration: Tokens.motion.press
                easing.type: Tokens.motion.easing
            }
        }

        Rectangle {
            id: knob
            width: 16
            height: 16
            radius: 8
            y: 2
            x: root.checked ? track.width - width - 2 : 2
            color: root.checked ? Tokens.on : (Tokens.dark ? Tokens.faint : Tokens.panel)
            Behavior on x {
                NumberAnimation {
                    duration: Tokens.motion.press
                    easing.type: Tokens.motion.easing
                }
            }
        }
    }

    HoverHandler { cursorShape: root.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor }

    TapHandler {
        enabled: root.interactive
        onTapped: {
            root.checked = !root.checked;
            root.toggled(root.checked);
        }
    }
}
