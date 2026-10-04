import QtQuick

/**
 * The light values of every colour token, from UI.md section 3.1.
 *
 * Nothing reads this file by name: Tokens builds both themes and forwards the active one's
 * properties under the token names the surfaces use, so a surface still asks for `Tokens.panel`
 * and never for a colour. A third theme is one more file here and one more arm in Tokens.
 */
QtObject {
    readonly property color bg: "#f6f6f8"
    readonly property color panel: "#ffffff"
    readonly property color panel2: "#fafafb"
    readonly property color line: "#e9e9ee"
    readonly property color line2: "#f2f2f5"
    readonly property color text: "#1a1a1f"
    readonly property color muted: "#66666f"
    readonly property color faint: "#9d9da8"
    readonly property color ok: "#2f9e6e"
    readonly property color okBg: "#e6f4ee"
    readonly property color okText: "#1d7a52"
    readonly property color ink: "#1a1a1f"
    readonly property color on: "#ffffff"
    readonly property color danger: "#b3564a"

    /**
     * The bubble's frosted fill and its border.
     *
     * The bubble is the one card that floats over the desktop rather than over another surface,
     * so its border is a glass edge -- lighter than the fill -- not a separator. Depth comes
     * from the drop shadow the surface draws under it.
     */
    readonly property color bubbleBg: Qt.rgba(1, 1, 1, 0.86)
    readonly property color bubbleBorder: Qt.rgba(1, 1, 1, 0.95)
}
