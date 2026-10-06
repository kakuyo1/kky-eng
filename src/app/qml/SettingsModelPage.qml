import QtQuick

Item {
    id: root
    property var settings: Controller.settings

    signal childRequested(string route)
    readonly property var openChildRect: providerField.openChildRect
    implicitHeight: content.implicitHeight

    function closeChild() {
        providerField.closeList();
    }

    Column {
        id: content
        width: parent.width
        spacing: 16

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Provider")
                color: Tokens.muted
                font.pixelSize: 12
            }
            DropdownField {
                id: providerField
                objectName: "providerField"
                width: parent.width
                options: root.settings.providers || []
                currentValue: root.settings.provider
                onPicked: (value) => Controller.setProvider(value)
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Model")
                color: Tokens.muted
                font.pixelSize: 12
            }

            // Typed rather than picked: the catalog names no model, so the reader supplies the
            // one their account carries and the address above is the only thing a provider
            // choice moves.
            Rectangle {
                width: parent.width
                height: 35
                radius: Tokens.radiusField
                color: Tokens.panel2
                border.width: 1
                border.color: modelInput.activeFocus ? Tokens.ink : Tokens.line

                TextInput {
                    id: modelInput
                    objectName: "modelField"
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    verticalAlignment: TextInput.AlignVCenter
                    text: root.settings.model
                    color: Tokens.text
                    font.pixelSize: 13
                    selectByMouse: true
                    onEditingFinished: {
                        Controller.setModel(text);
                        text = Qt.binding(() => root.settings.model);
                    }
                }
            }

            Text {
                width: parent.width
                text: root.settings.modelPrice || ""
                color: Tokens.muted
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
        }

        SettingsLinkRow {
            label: qsTranslate("SettingsPopup", "API configuration")
            note: root.settings.hasApiKey
                   ? qsTranslate("SettingsPopup", "Configured")
                   : qsTranslate("SettingsPopup", "Not configured")
            onPicked: root.childRequested("connection")
        }
    }
}
