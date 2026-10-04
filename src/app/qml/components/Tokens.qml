pragma Singleton

import QtQuick

/**
 * The colour and type table every surface reads, from UI.md section 3.
 *
 * Surfaces never name a colour directly: they ask for a token, and the light or dark value
 * comes back. Switching the theme is one assignment to theme, and nothing else has to know.
 */
QtObject {
    id: tokens

    /// "light" or "dark". main.qml keeps this in step with the settings popup.
    property string theme: "light"
    readonly property bool dark: theme === "dark"

    readonly property color bg: dark ? "#131316" : "#f6f6f8"
    readonly property color panel: dark ? "#1c1c20" : "#ffffff"
    readonly property color panel2: dark ? "#242429" : "#fafafb"
    readonly property color line: dark ? "#2f2f37" : "#e9e9ee"
    readonly property color line2: dark ? "#26262c" : "#f2f2f5"
    readonly property color text: dark ? "#e9e9ee" : "#1a1a1f"
    readonly property color muted: dark ? "#a3a3ae" : "#66666f"
    readonly property color faint: dark ? "#75757f" : "#9d9da8"
    readonly property color ok: dark ? "#45c08a" : "#2f9e6e"
    readonly property color okBg: dark ? "#1a3527" : "#e6f4ee"
    readonly property color okText: dark ? "#6fd6a5" : "#1d7a52"
    readonly property color ink: dark ? "#e9e9ee" : "#1a1a1f"
    readonly property color on: dark ? "#16161a" : "#ffffff"
    readonly property color danger: dark ? "#d98d81" : "#b3564a"

    /// The bubble's frosted fill and its border. In the dark theme the border is the only
    /// thing separating the bubble from what is behind it, so it cannot be dropped.
    readonly property color bubbleBg: dark ? Qt.rgba(30 / 255, 30 / 255, 35 / 255, 0.82)
                                           : Qt.rgba(1, 1, 1, 0.86)
    readonly property color bubbleBorder: dark ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(1, 1, 1, 0.95)

    /// Rounded corners, largest to smallest (UI.md section 3.3).
    readonly property int radiusCard: 18
    readonly property int radiusMenu: 13
    readonly property int radiusGroup: 11
    readonly property int radiusField: 9
    readonly property int radiusPill: 999

    /**
     * The figures' face. The UI face is not here: it is the application font, set in main.cpp,
     * because QML cannot express what it needs to be.
     *
     * A Text names at most one family, and naming one replaces whatever it would have
     * inherited. The application prefers one face for Latin and CJK; MixedText is the exception
     * that deliberately splits a localized value into separate font runs.
     *
     * Components that need a monospaced face use this family for non-CJK runs only.
     */
    readonly property string monoFamily: "Cascadia Code"
}
