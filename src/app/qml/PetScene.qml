import QtQuick

/**
 * The dog as layers, at the frame the controller chose. Body, expression and effect take their z from pet.json;
 * accessories take theirs from accessories.json. The scene scales by a whole number, so pixels stay square.
 */
Item {
    width: Pet.canvas
    height: Pet.canvas
    scale: Pet.scale
    transformOrigin: Item.TopLeft

    SpriteLayer {
        z: Pet.layers.body
        sheet: Pet.sheets.body
        frame: Pet.frameIndex
        at: Pet.anchors.body
    }

    SpriteLayer {
        z: Pet.layers.expression
        sheet: Pet.sheets.expression
        frame: Pet.expression
        at: Pet.anchors.face
    }

    SpriteLayer {
        z: Pet.layers.effect
        sheet: Pet.sheets.effect
        frame: Pet.frameIndex
        at: Qt.point(0, 0)
    }

    Repeater {
        model: Pet.wornAccessories

        SpriteLayer {
            required property var modelData

            z: modelData.zIndex
            sheet: modelData.asset
            frame: 0
            at: Qt.point(modelData.x, modelData.y)
        }
    }
}
