import QtQuick
import QtQuick.Effects

/**
 * The rounded card every panel is drawn on, with the drop shadow UI.md section 3.3 gives it.
 *
 * The window around this has to be `shadowMargin` larger on every side than the card: the
 * blur needs room to fall off, and a frameless window gets no shadow from the system. Callers
 * place their content with `anchors.fill: parent` plus the same margin.
 */
Item {
    id: root

    /// Room the shadow needs. The window is this much bigger than the card on each side.
    readonly property int shadowMargin: 26

    default property alias content: holder.data
    property real radius: Tokens.radiusCard
    property color color: Tokens.panel

    /// Whether the reader may drag the card to move the window it is in.
    property bool movable: false

    /// True while a drag is moving the window. Surfaces that time out watch it: a card being
    /// held is not on its way out.
    readonly property alias dragging: mover.active

    /**
     * The handle a drag moves the window by.
     *
     * @note Neither the system's move loop nor the handler's own translation can do this, both
     *       for a reason measured on the real window. startSystemMove() left the window exactly
     *       where it was for the whole drag and put it down on release -- a jump, not a drag.
     *       `activeTranslation` is measured *inside* the window, so moving the window changes
     *       the translation that decided the move: at hand speed the panel went from x=1116 to
     *       x=-3688 within a few dozen events. What is left is the pointer's own screen
     *       position, which does not move when the window does.
     */
    DragHandler {
        id: mover
        enabled: root.movable
        target: null

        /// Where the pointer was on screen, and where the window was, when the drag began.
        property point grabCursor: Qt.point(0, 0)
        property point grabWindow: Qt.point(0, 0)

        /// @brief Put the window where the pointer says it should be, from this drag's grab.
        function place() {
            const at = controller.cursorPos();
            root.Window.window.x = Math.round(grabWindow.x + at.x - grabCursor.x);
            root.Window.window.y = Math.round(grabWindow.y + at.y - grabCursor.y);
        }

        onActiveChanged: {
            if (active) {
                grabCursor = controller.cursorPos();
                grabWindow = Qt.point(root.Window.window.x, root.Window.window.y);
            } else {
                // Once more on the way out, so the movement after the last tick is not left
                // behind: the pointer stops moving when the button comes up, not when the
                // timer last looked.
                place();
            }
        }
    }

    /// Sampled on a timer rather than driven by the handler's change signal: once the window
    /// keeps up with the pointer the translation stops changing, so the signal stops, and the
    /// rest of the drag would never be applied -- measured as a drag that covered only 104 of
    /// its 144 device-independent pixels.
    Timer {
        interval: 16
        repeat: true
        running: mover.active
        onTriggered: mover.place()
    }

    /// The card's rectangle inside this item, for anchoring content to.
    readonly property alias card: card

    // The shadow is cast from a shape-only copy of the card, never from the card itself.
    // MultiEffect draws its source through an offscreen texture, and at this monitor's 125%
    // scale that texture is resampled: measured on the real window, one glyph stem comes out
    // twice as wide and half as dark as the same text drawn directly. The shape can afford it;
    // the text cannot. Both the shape and the effect are declared before the card so they paint
    // under it.
    Rectangle {
        id: shadowShape
        anchors.fill: card
        radius: root.radius
        color: root.color
        visible: false
    }

    MultiEffect {
        // Sized and placed by the effect itself, from its source: MultiEffect fills its own
        // rectangle with the source, so anchoring it to the window (bigger than the card)
        // stretched the card's shape across the whole surface -- an opaque band inside the
        // shadow margin, which is the "extra background layer" the panels were showing. Left
        // unsized, it grows itself by what the blur needs, and the shape maps 1:1.
        x: card.x
        y: card.y
        source: shadowShape
        shadowEnabled: true
        shadowColor: Qt.rgba(20 / 255, 20 / 255, 26 / 255, Tokens.dark ? 0.75 : 0.24)
        shadowBlur: 0.9
        shadowVerticalOffset: 10
        blurMax: 40
    }

    Rectangle {
        id: card
        x: root.shadowMargin
        y: root.shadowMargin
        width: root.width - 2 * root.shadowMargin
        height: root.height - 2 * root.shadowMargin
        radius: root.radius
        color: root.color
        border.width: 1
        border.color: Tokens.line

        Item {
            id: holder
            anchors.fill: parent
        }
    }
}
