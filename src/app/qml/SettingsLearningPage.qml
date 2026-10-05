import QtQuick

Item {
    id: root

    readonly property var openChildRect: levelField.openChildRect
    implicitHeight: content.implicitHeight

    function closeChild() {
        levelField.closeList();
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
                options: Controller.settings.levels
                currentValue: Controller.settings.level
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
            Segment {
                width: parent.width
                height: 31
                labels: [qsTranslate("SettingsPopup", "English"), qsTranslate("SettingsPopup", "中文")]
                currentIndex: Controller.settings.explanationLang === "zh" ? 1 : 0
                onPicked: (index) => Controller.setExplanationLang(index === 1 ? "zh" : "en")
            }
        }

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Multiple senses")
            checked: Controller.settings.multiSense
            onToggled: (on) => Controller.setMultiSense(on)
        }
    }
}
