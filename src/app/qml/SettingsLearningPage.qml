import QtQuick

Item {
    id: root
    property var settings: Controller.settings

    readonly property var openChildRect: levelField.openChildRect || languageField.openChildRect
    implicitHeight: content.implicitHeight

    function closeChild() {
        levelField.closeList();
        languageField.closeList();
    }

    Column {
        id: content
        width: parent.width
        spacing: 16

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Vocabulary level")
                color: Tokens.muted
                font.pixelSize: 12
            }
            DropdownField {
                id: levelField
                width: parent.width
                options: root.settings.levels
                currentValue: root.settings.level
                onPicked: (value) => Controller.setLevel(value)
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Explanation language")
                color: Tokens.muted
                font.pixelSize: 12
            }
            DropdownField {
                id: languageField
                objectName: "languageField"
                width: parent.width
                options: root.settings.languages || []
                currentValue: root.settings.explanationLang
                onPicked: (value) => Controller.setExplanationLang(value)
            }
        }

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Multiple senses")
            checked: root.settings.multiSense
            onToggled: (on) => Controller.setMultiSense(on)
        }
    }
}
