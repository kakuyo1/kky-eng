import QtQuick

Item {
    id: root

    signal childRequested(string route)
    readonly property var openChildRect: providerField.openChildRect
    implicitHeight: content.implicitHeight

    function closeChild() {
        providerField.closeList();
    }

    readonly property var providers: [
        { value: "DeepSeek", label: "DeepSeek", group: qsTranslate("SettingsPopup", "Domestic"), note: "" },
        { value: "qwen", label: qsTranslate("SettingsPopup", "Qwen"), group: qsTranslate("SettingsPopup", "Domestic"), note: "" },
        { value: "glm", label: qsTranslate("SettingsPopup", "GLM"), group: qsTranslate("SettingsPopup", "Domestic"), note: "" },
        { value: "kimi", label: "Kimi", group: qsTranslate("SettingsPopup", "Domestic"), note: "" },
        { value: "doubao", label: qsTranslate("SettingsPopup", "Doubao"), group: qsTranslate("SettingsPopup", "Domestic"), note: "" },
        { value: "openai", label: "OpenAI", group: qsTranslate("SettingsPopup", "International"), note: "" },
        { value: "anthropic", label: "Anthropic", group: qsTranslate("SettingsPopup", "International"), note: "" },
        { value: "gemini", label: "Google Gemini", group: qsTranslate("SettingsPopup", "International"), note: "" },
        { value: "openrouter", label: "OpenRouter", group: qsTranslate("SettingsPopup", "Other"), note: "" },
        { value: "custom", label: qsTranslate("SettingsPopup", "Custom service"), group: qsTranslate("SettingsPopup", "Other"), note: "" }
    ]

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
                width: parent.width
                options: root.providers
                currentValue: Controller.settings.provider || "DeepSeek"
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
                    text: Controller.settings.model
                    color: Tokens.text
                    font.pixelSize: 13
                    selectByMouse: true
                    onEditingFinished: Controller.setModel(text)
                }
            }
        }

        SettingsLinkRow {
            label: qsTranslate("SettingsPopup", "API configuration")
            note: Controller.settings.hasApiKey
                   ? qsTranslate("SettingsPopup", "Configured")
                   : qsTranslate("SettingsPopup", "Not configured")
            onPicked: root.childRequested("connection")
        }
    }
}
