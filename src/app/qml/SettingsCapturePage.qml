import QtQuick

Item {
    id: root

    signal childRequested(string route)
    readonly property var openChildRect: null
    implicitHeight: content.implicitHeight

    function closeChild() {}

    Column {
        id: content
        width: parent.width
        spacing: 14

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Trigger channels")
                color: Tokens.muted
                font.pixelSize: 11
            }
            Rectangle {
                width: parent.width
                implicitHeight: switches.implicitHeight + 8
                radius: Tokens.radiusGroup
                color: Tokens.panel2
                border.width: 1
                border.color: Tokens.line

                Column {
                    id: switches
                    x: 12
                    y: 4
                    width: parent.width - 24
                    spacing: 0

                    SwitchRow {
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "Selection")
                        checked: Controller.settings.selectionCapture
                        onToggled: (on) => Controller.setSelectionCapture(on)
                    }
                    SwitchRow {
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "OCR")
                        checked: false
                        interactive: false
                    }
                    SwitchRow {
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "Auto scan")
                        checked: false
                        interactive: false
                    }
                }
            }
        }

        Item {
            width: parent.width
            height: 35
            opacity: 0.5

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTranslate("SettingsPopup", "Toggle auto scan")
                color: Tokens.faint
                font.pixelSize: 13
            }
            Rectangle {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                height: 21
                radius: 6
                color: Tokens.panel
                border.width: 1
                border.color: Tokens.line
                Text {
                    anchors.centerIn: parent
                    text: "F8"
                    color: Tokens.muted
                    font.pixelSize: 11
                }
            }
        }

        SettingsLinkRow {
            label: qsTranslate("SettingsPopup", "Popup & clipboard")
            note: Controller.settings.clipboardPolicy === "silent"
                  ? qsTranslate("SettingsPopup", "Give up silently")
                  : qsTranslate("SettingsPopup", "Raise to top")
            onPicked: root.childRequested("clipboard")
        }
    }
}
