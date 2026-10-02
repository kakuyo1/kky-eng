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

    /// The card's rectangle inside this item, for anchoring content to.
    readonly property alias card: card

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

    MultiEffect {
        anchors.fill: parent
        source: card
        shadowEnabled: true
        shadowColor: Qt.rgba(20 / 255, 20 / 255, 26 / 255, Tokens.dark ? 0.75 : 0.24)
        shadowBlur: 0.9
        shadowVerticalOffset: 10
        blurMax: 40
    }
}
