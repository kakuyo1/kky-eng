import QtQuick

/**
 * One sprite sheet, drawn as a single cell. The frame index picks the cell; every layer reads the same index, so
 * the layers never fall out of step. Nearest-neighbour keeps the pixel edges sharp.
 */
Image {
    required property string sheet
    required property int frame
    required property point at

    x: at.x
    y: at.y
    width: Pet.canvas
    height: Pet.canvas
    source: sheet
    sourceClipRect: Qt.rect(frame * Pet.canvas, 0, Pet.canvas, Pet.canvas)
    smooth: false
    visible: sheet !== ""
}
