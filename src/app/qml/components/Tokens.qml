pragma Singleton

import QtQuick

/**
 * The colour, type and motion table every surface reads, from UI.md section 3 and its motion
 * table.
 *
 * The colours themselves live in qml/theme/, one file per theme; this file builds both tables
 * and forwards the active one's values under the token names the surfaces use. A surface never
 * names a colour directly: it asks for a token, and the light or dark value comes back.
 * Switching the theme is one assignment to theme, and nothing else has to know. The durations
 * and curves in qml/theme/Motion.qml come through the same way, under `motion`.
 */
QtObject {
    id: tokens

    /// "light" or "dark". main.qml keeps this in step with the settings popup.
    property string theme: "light"
    readonly property bool dark: theme === "dark"

    /// The two value tables, both built and both read: `dark` picks which one the tokens below
    /// come from. A third theme is another file in qml/theme/ and one more arm in each line.
    readonly property Light lightTheme: Light {}
    readonly property Dark darkTheme: Dark {}

    /// Durations and curves, built and forwarded the same way: theme/Motion.qml holds them,
    /// and a surface reads Tokens.motion.<token> rather than writing a millisecond itself.
    /// Whether they are all zero is Windows' answer, read once by SystemMotion.
    readonly property Motion motion: Motion {}

    readonly property color bg: dark ? darkTheme.bg : lightTheme.bg
    readonly property color panel: dark ? darkTheme.panel : lightTheme.panel
    readonly property color panel2: dark ? darkTheme.panel2 : lightTheme.panel2
    readonly property color line: dark ? darkTheme.line : lightTheme.line
    readonly property color line2: dark ? darkTheme.line2 : lightTheme.line2
    readonly property color text: dark ? darkTheme.text : lightTheme.text
    readonly property color muted: dark ? darkTheme.muted : lightTheme.muted
    readonly property color faint: dark ? darkTheme.faint : lightTheme.faint
    readonly property color ok: dark ? darkTheme.ok : lightTheme.ok
    readonly property color okBg: dark ? darkTheme.okBg : lightTheme.okBg
    readonly property color okText: dark ? darkTheme.okText : lightTheme.okText
    readonly property color ink: dark ? darkTheme.ink : lightTheme.ink
    readonly property color on: dark ? darkTheme.on : lightTheme.on
    readonly property color danger: dark ? darkTheme.danger : lightTheme.danger

    /// The bubble's frosted fill and its border -- the two themes' values and why they differ
    /// are in theme/Light.qml and theme/Dark.qml.
    readonly property color bubbleBg: dark ? darkTheme.bubbleBg : lightTheme.bubbleBg
    readonly property color bubbleBorder: dark ? darkTheme.bubbleBorder : lightTheme.bubbleBorder

    /// Rounded corners, largest to smallest (UI.md section 3.3). The same in both themes, so
    /// they are not part of a theme table.
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
