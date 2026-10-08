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

        // Beside "multiple senses" rather than in a category of its own: both decide what a word's
        // explanation is made of, and one is read as a richer version of the other.
        SwitchRow {
            width: parent.width
            objectName: "etymologySwitch"
            label: qsTranslate("SettingsPopup", "Etymology")
            checked: root.settings.etymology
            onToggled: (on) => Controller.setEtymology(on)
        }
    }
}
