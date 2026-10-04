import QtQuick

/**
 * The dark values of every colour token, from UI.md section 3.1b.
 *
 * Unlike light, where the card's shadow carries most of the depth, the dark theme's shadow is
 * nearly invisible on dark desktops: the lightness step between the surfaces plus the border
 * is what separates a card from what is behind it, so `line` is lighter than `bg` here and is
 * not a colour to tune away. `ink` and `on` invert -- on dark the primary action is the light
 * fill, not the dark one -- and `text` stops short of pure white, which blooms on a dark desk.
 */
QtObject {
    readonly property color bg: "#131316"
    readonly property color panel: "#1c1c20"
    readonly property color panel2: "#242429"
    readonly property color line: "#2f2f37"
    readonly property color line2: "#26262c"
    readonly property color text: "#e9e9ee"
    readonly property color muted: "#a3a3ae"
    readonly property color faint: "#75757f"
    readonly property color ok: "#45c08a"
    readonly property color okBg: "#1a3527"
    readonly property color okText: "#6fd6a5"
    readonly property color ink: "#e9e9ee"
    readonly property color on: "#16161a"
    readonly property color danger: "#d98d81"

    /// The same pair as in light, with the border doing the work the shadow does there: on dark
    /// the frosted fill is very close to what shows through it, and the 9% white edge is the
    /// only thing that reads as an edge at all.
    readonly property color bubbleBg: Qt.rgba(30 / 255, 30 / 255, 35 / 255, 0.82)
    readonly property color bubbleBorder: Qt.rgba(1, 1, 1, 0.09)
}
