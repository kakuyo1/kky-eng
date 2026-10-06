import QtQuick

Item {
    id: root
    property var settings: Controller.settings

    signal childRequested(string route)
    readonly property var openChildRect: providerField.openChildRect || modelField.openChildRect
    implicitHeight: content.implicitHeight

    function closeChild() {
        providerField.closeList();
        modelField.closeList();
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
            Rectangle {
                visible: root.settings.provider === "custom"
                width: parent.width
                height: 35
                radius: Tokens.radiusField
                color: Tokens.panel2
                border.width: 1
                border.color: Tokens.line

                TextInput {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    verticalAlignment: TextInput.AlignVCenter
                    text: root.settings.model
                    color: Tokens.text
                    font.pixelSize: 13
                    selectByMouse: true
                    onEditingFinished: Controller.setModel(text)
                }
            }
            DropdownField {
                id: modelField
                objectName: "modelField"
                visible: root.settings.provider !== "custom"
                width: parent.width
                options: root.settings.models || []
                currentValue: root.settings.model
                onPicked: (value) => Controller.setModel(value)
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
