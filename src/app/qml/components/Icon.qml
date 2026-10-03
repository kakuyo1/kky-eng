import QtQuick
import QtQuick.Window
import QtQuick.Effects

/**
 * One glyph, drawn from the SVG file of the same name under icons/.
 *
 * The art is a 24x24 outline with no fill, stroked in white and scaled to whatever size the
 * surface asks for. The colour comes from the colorization below rather than from the file, so
 * one file serves the faint, muted and danger roles and the theme switches all of them at once.
 */
Item {
    id: root

    property string source: ""
    property color color: Tokens.muted

    implicitWidth: 15
    implicitHeight: 15

    // The source of the effect, never drawn itself, for the reason ShadowCard.qml gives: a
    // MultiEffect renders its source through an offscreen texture.
    Image {
        id: glyph
        anchors.fill: parent
        source: root.source
        // Rasterised at the size it is drawn at and at the screen's own scale. The 24x24 the
        // file declares is a coordinate space, not a size to hand to the renderer.
        sourceSize: Qt.size(Math.max(1, Math.round(width * Screen.devicePixelRatio)),
                            Math.max(1, Math.round(height * Screen.devicePixelRatio)))
        visible: false
    }

    MultiEffect {
        anchors.fill: glyph
        source: glyph
        colorizationColor: root.color
        colorization: 1.0
    }
}
