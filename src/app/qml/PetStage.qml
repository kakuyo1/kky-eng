import QtQuick
import QtQuick.Window

/**
 * The settings preview: the same dog as the desktop, on a baseline, at the size the reader chose. The clock
 * runs only while this stage is on a visible settings window, so a closed settings panel costs nothing.
 *
 * It shares Pet with the desktop window, so an action picked here plays on the desktop too while the
 * settings stay open. That is the one dog; a second clock would only fight the first for frames.
 */
Rectangle {
    id: stage

    readonly property bool onScreen: visible && Window.window !== null && Window.window.visible
    readonly property int dogSize: Pet.canvas * Pet.scale

    implicitHeight: dogSize + 24
    radius: Tokens.radiusGroup
    color: Tokens.panel2
    border.width: 1
    border.color: Tokens.line2
    clip: true

    onOnScreenChanged: Pet.setPreviewing(onScreen)
    Component.onCompleted: Pet.setPreviewing(onScreen)
    // Leaving the page (another category) ends the preview too, as closing the window does.
    Component.onDestruction: Pet.setPreviewing(false)

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        height: 1
        color: Tokens.line
    }

    PetScene {
        x: (stage.width - stage.dogSize) / 2
        y: stage.height - stage.dogSize - 12
    }
}
