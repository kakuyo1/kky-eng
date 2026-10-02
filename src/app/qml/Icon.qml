import QtQuick
import QtQuick.Shapes

/**
 * One stroked glyph, drawn from the path data the prototypes carry.
 *
 * The prototypes in ui-prototypes/ are the source of these paths: they are 24x24 outlines
 * with no fill, scaled down to whatever size the surface asks for.
 */
Item {
    id: root

    property string path: ""
    property real strokeWidth: 1.6
    property color color: Tokens.muted

    implicitWidth: 15
    implicitHeight: 15

    Shape {
        width: 24
        height: 24
        scale: Math.min(root.width, root.height) / 24
        transformOrigin: Item.TopLeft

        ShapePath {
            strokeColor: root.color
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.path }
        }
    }
}
