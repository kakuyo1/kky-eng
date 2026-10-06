import QtQuick

/**
 * A green-leaning neutral theme for readers who want more separation from the light and dark
 * defaults. The text roles are tuned against the panel rather than the canvas.
 */
QtObject {
    readonly property color bg: "#0c1715"
    readonly property color panel: "#152522"
    readonly property color panel2: "#1f3430"
    readonly property color line: "#35554b"
    readonly property color line2: "#27413b"
    readonly property color text: "#e8f2ee"
    readonly property color muted: "#b8cec5"
    readonly property color faint: "#9bb9ad"
    readonly property color ok: "#67c596"
    readonly property color okBg: "#1d4935"
    readonly property color okText: "#9be0bb"
    readonly property color ink: "#e6f2ec"
    readonly property color on: "#10201b"
    readonly property color danger: "#f0a18e"
    readonly property color bubbleBg: Qt.rgba(25 / 255, 43 / 255, 39 / 255, 0.94)
    readonly property color bubbleBorder: Qt.rgba(190 / 255, 235 / 255, 216 / 255, 0.22)
}
