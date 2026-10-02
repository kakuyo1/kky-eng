import QtQuick

/**
 * One labelled switch inside the capture group (UI.md 4.4). A non-interactive row keeps its
 * label in the faint colour and its switch dimmed, with nothing said about why.
 */
Row {
    id: root

    property string label: ""
    property bool checked: false
    property bool interactive: true

    signal toggled(bool checked)

    width: parent ? parent.width : implicitWidth
    height: 33
    spacing: 8

    Text {
        id: labelText
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        color: root.interactive ? Tokens.text : Tokens.faint
        font.family: Tokens.fontFamily
        font.pixelSize: 13
    }

    Item {
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, root.width - labelText.width - switcher.width - 2 * root.spacing)
        height: 1
    }

    Switch {
        id: switcher
        anchors.verticalCenter: parent.verticalCenter
        checked: root.checked
        interactive: root.interactive
        onToggled: (on) => root.toggled(on)
    }
}
