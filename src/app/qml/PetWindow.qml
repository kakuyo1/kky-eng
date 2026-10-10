import QtQuick

/**
 * The desktop pet's own window: frameless, on top, never focused, kept out of screen capture. A press that does not
 * move is a click; a press that moves is a pickup, followed by a drop that remembers where it landed.
 */
Window {
    id: petWindow

    readonly property real cursorSlop: 4
    property point grabCursor
    property point grabAt
    property bool moved: false

    width: Pet.canvas * Pet.scale
    height: Pet.canvas * Pet.scale
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: Pet.enabled

    onVisibleChanged: Pet.setRunning(visible)
    onWidthChanged: place()

    Component.onCompleted: {
        Pet.attachWindow(petWindow);
        place();
    }

    /// Opens at the saved spot, or the corner when that no longer fits, so a bigger size cannot hang off the screen.
    function place() {
        const at = Pet.initialPosition(Qt.size(width, height));
        x = at.x;
        y = at.y;
    }

    PetScene {
        anchors.fill: parent
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton

        onPressed: {
            petWindow.grabCursor = Pet.cursorPos();
            petWindow.grabAt = Qt.point(petWindow.x, petWindow.y);
            petWindow.moved = false;
        }

        onPositionChanged: {
            if (!pressed)
                return;
            const cursor = Pet.cursorPos();
            const dx = cursor.x - petWindow.grabCursor.x;
            const dy = cursor.y - petWindow.grabCursor.y;
            if (!petWindow.moved) {
                if (Math.abs(dx) + Math.abs(dy) < petWindow.cursorSlop)
                    return;
                petWindow.moved = true;
                Pet.dragStart();
            }
            petWindow.x = petWindow.grabAt.x + dx;
            petWindow.y = petWindow.grabAt.y + dy;
        }

        onReleased: {
            if (petWindow.moved) {
                Pet.dragEnd();
                Pet.savePosition(Qt.point(petWindow.x, petWindow.y));
            } else {
                Pet.click();
            }
        }
    }
}
