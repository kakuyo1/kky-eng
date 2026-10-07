pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The dimming mask the capture trigger key raises, and the rectangle the reader drags in it.
 *
 * One window per screen rather than one across the desktop: the application is per-monitor DPI
 * aware, and a window spanning two monitors of different scale renders at one of them, which is the
 * same reason the surfaces work their anchors out per screen (docs/QML.md section 4). A drag
 * therefore begins and ends on one screen, which is also what the capture takes -- a rectangle
 * wholly inside one screen, in the device-independent coordinates QScreen geometry uses.
 *
 * The mask is off the screen before any pixels are read. `QScreen::grabWindow` reads the screen DC,
 * so whatever is composited on top -- this mask -- would be in the picture, and the reader would get
 * the dim instead of their text. That is what the settle delay below is for.
 *
 * It is an Item rather than a Window because what it holds is the Repeater: a surface whose root is
 * not a window is not a surface, and the one window per screen is what the delegate is for.
 */
Item {
    id: root

    /// The mask is on screen. Raised and taken down by the trigger key and by a finished drag.
    property bool shown: false

    /// A rectangle was dragged. Carried in the device-independent desktop coordinates the capture
    /// takes, and the mask is already off the screen when this arrives.
    signal regionChosen(rect region)

    /// How long the mask waits after hiding before the region is asked for. Longer than a frame
    /// (the compositor has to have presented the desktop without us) and shorter than a reader can
    /// notice. Named rather than inlined because it is the one number that would have to move if a
    /// machine turned out to be slower than this one.
    readonly property int settleMs: 120

    /// What the reader has not framed is covered with this. A mask is over other programs' windows
    /// and has no theme of its own, so it is not a Tokens colour: it has to read the same over the
    /// light and the dark desktop alike.
    readonly property color shade: Qt.rgba(0, 0, 0, 0.38)
    /// The frame around the selected region, and the hint over the shade.
    readonly property color frame: "#ffffff"

    property rect pendingRegion: Qt.rect(0, 0, 0, 0)

    function open() { root.shown = true; }
    function close() { root.shown = false; }

    // Instantiator rather than Repeater: what it builds is a Window per screen, and a Repeater's
    // delegate has to be an Item.
    //
    // The model is the screen *count*, not the screens: an Instantiator's model is a number, a list
    // or an item model, and `Qt.application.screens` is a QML list property of screen objects --
    // handed straight to it the delegate is built zero times, silently. Each sheet reads its own
    // screen back by index instead, which is also what makes a monitor being plugged in or
    // unplugged rebuild the set: the count changes and the index reads follow it.
    Instantiator {
        // qmllint disable missing-property
        model: Qt.application.screens.length

        delegate: Window {
            id: sheet
            required property int index

            objectName: "maskSheet"
            readonly property var screenInfo: Qt.application.screens[index]
            // qmllint enable missing-property

            x: sheet.screenInfo ? sheet.screenInfo.virtualX : 0
            y: sheet.screenInfo ? sheet.screenInfo.virtualY : 0
            width: sheet.screenInfo ? sheet.screenInfo.width : 1
            height: sheet.screenInfo ? sheet.screenInfo.height : 1
            visible: root.shown
            color: "transparent"
            // Not WindowDoesNotAcceptFocus like the other surfaces: this one is asking the reader
            // for a gesture, and it is where the pointer has to be able to land.
            flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

            /// Whether the pointer is down on this screen, and where it went down.
            property bool dragging: false
            property point origin: Qt.point(0, 0)
            /// What the reader has framed, in this screen's own coordinates.
            property rect selection: Qt.rect(0, 0, 0, 0)

            onVisibleChanged: {
                if (visible) {
                    sheet.raise();
                    sheet.requestActivate();
                    return;
                }
                sheet.dragging = false;
                sheet.selection = Qt.rect(0, 0, 0, 0);
            }

            /// @return The drag from where it started to @p x / @p y, kept on this screen.
            ///         A pointer that runs off the edge keeps framing: the capture takes no
            ///         rectangle that leaves the screen it began on.
            function framedTo(x, y) {
                const left = Math.max(0, Math.min(sheet.origin.x, x));
                const top = Math.max(0, Math.min(sheet.origin.y, y));
                const right = Math.min(sheet.width, Math.max(sheet.origin.x, x));
                const bottom = Math.min(sheet.height, Math.max(sheet.origin.y, y));
                return Qt.rect(Math.round(left), Math.round(top), Math.round(right - left), Math.round(bottom - top));
            }

            // Four bands rather than one rectangle with a hole in it: the framed area is the one
            // place the reader has to see the real screen, and a transparent rectangle cannot take
            // a dim away. Before the drag every band but the last is empty, and the last is the
            // whole screen.
            Rectangle { width: sheet.width; height: sheet.selection.y; color: root.shade }
            Rectangle { y: sheet.selection.y; width: sheet.selection.x; height: sheet.selection.height; color: root.shade }
            Rectangle {
                x: sheet.selection.x + sheet.selection.width
                y: sheet.selection.y
                width: Math.max(0, sheet.width - sheet.selection.x - sheet.selection.width)
                height: sheet.selection.height
                color: root.shade
            }
            Rectangle {
                y: sheet.selection.y + sheet.selection.height
                width: sheet.width
                height: Math.max(0, sheet.height - sheet.selection.y - sheet.selection.height)
                color: root.shade
            }

            Rectangle {
                visible: sheet.dragging
                x: sheet.selection.x
                y: sheet.selection.y
                width: sheet.selection.width
                height: sheet.selection.height
                color: "transparent"
                border.width: 1
                border.color: root.frame
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: Math.round(sheet.height * 0.12)
                visible: !sheet.dragging
                text: qsTr("Drag to frame the text to read · Esc cancels")
                color: root.frame
                font.pixelSize: 13
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.CrossCursor

                onPressed: (mouse) => {
                    sheet.origin = Qt.point(mouse.x, mouse.y);
                    sheet.selection = Qt.rect(mouse.x, mouse.y, 0, 0);
                    sheet.dragging = true;
                }
                onPositionChanged: (mouse) => sheet.selection = sheet.framedTo(mouse.x, mouse.y)
                onReleased: (mouse) => {
                    const framed = sheet.framedTo(mouse.x, mouse.y);
                    sheet.dragging = false;
                    root.close();
                    // A press that did not travel is the reader changing their mind, not a capture
                    // of nothing: the mask goes and the shot is not asked for.
                    if (framed.width < 2 || framed.height < 2)
                        return;
                    root.pendingRegion = Qt.rect(sheet.screenInfo.virtualX + framed.x, sheet.screenInfo.virtualY + framed.y, framed.width, framed.height);
                    settle.restart();
                }
            }
        }
    }

    /// Application-wide rather than window-scoped, the way the removal question's is: the offscreen
    /// platform the suite runs on never gives a window the active state, and `shown` is the guard.
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: root.shown
        onActivated: root.close()
    }

    Timer {
        id: settle
        interval: root.settleMs
        onTriggered: root.regionChosen(root.pendingRegion)
    }
}
