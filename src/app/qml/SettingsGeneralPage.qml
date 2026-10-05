import QtQuick

Item {
    readonly property var openChildRect: null
    implicitHeight: content.implicitHeight

    function closeChild() {}

    Column {
        id: content
        width: parent.width
        spacing: 16

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Theme")
                color: Tokens.muted
                font.pixelSize: 12
            }
            Segment {
                width: parent.width
                height: 31
                labels: [qsTranslate("SettingsPopup", "Light"), qsTranslate("SettingsPopup", "Dark")]
                currentIndex: Controller.settings.theme === "dark" ? 1 : 0
                onPicked: (index) => Controller.setTheme(index === 1 ? "dark" : "light")
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Interface language")
                color: Tokens.muted
                font.pixelSize: 12
            }
            Segment {
                width: parent.width
                height: 31
                labels: [qsTranslate("SettingsPopup", "中文"), qsTranslate("SettingsPopup", "English")]
                currentIndex: Controller.settings.uiLanguage === "en" ? 1 : 0
                onPicked: (index) => Controller.setUiLanguage(index === 1 ? "en" : "zh")
            }
        }

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Launch at sign-in")
            checked: Controller.settings.autostart
            onToggled: (on) => Controller.setAutostart(on)
        }
    }
}
