import QtQuick

/**
 * How long a change takes, and what curve it rides, from UI.md's motion table.
 *
 * The durations are milliseconds and the surfaces never spell one: a Behaviour reads
 * `Tokens.motion.<token>`, so retiming the whole application is an edit here and nothing else.
 * Together with the two colour tables this is the third value table in qml/theme/, built and
 * forwarded by Tokens the same way they are.
 *
 * Reduced motion is not a CSS media query in this application. Windows exposes no
 * prefers-reduced-motion, and its preference comes from SystemParametersInfoW(
 * SPI_GETCLIENTAREAANIMATION) -- src/app/system_motion.h names the interface and reads it once.
 * The application switch can also reduce every duration to zero, but cannot override Windows'
 * choice to turn animations off.
 */
QtObject {
    /// Writable so a test can build one of these with a known answer rather than the machine value.
    property bool reduced: SystemMotion.reduced || !Controller.settings.animationsEnabled

    /// A button going down and coming back up, a switch crossing, a hover fill arriving.
    readonly property int press: reduced ? 0 : 150

    /// A surface appearing -- the panels, the menus, the notice -- and a row arriving in a
    /// list. Short: an appearance that is still running when the reader has already read the
    /// card is in the way.
    readonly property int pop: reduced ? 0 : 180

    /// The bubble's verdict row folding open and shut, which has further to travel than
    /// anything else here.
    readonly property int bubble: reduced ? 0 : 220

    /// The one curve. UI.md's table says "ease" and says no more than that.
    readonly property int easing: Easing.InOutQuad
}
