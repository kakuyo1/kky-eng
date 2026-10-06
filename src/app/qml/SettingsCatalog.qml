import QtQuick

Item {
    id: root

    signal categoryRequested(string category)
    readonly property var openChildRect: null
    implicitHeight: content.implicitHeight

    function closeChild() {}

    Column {
        id: content
        width: parent.width
        spacing: 2

        SettingsCategoryRow {
            title: qsTranslate("SettingsPopup", "General")
            summary: qsTranslate("SettingsPopup", "Theme, launch at startup")
            onPicked: root.categoryRequested("general")
        }
        SettingsCategoryRow {
            title: qsTranslate("SettingsPopup", "Reading & learning")
            summary: qsTranslate("SettingsPopup", "Vocabulary level, explanation language, multiple senses")
            onPicked: root.categoryRequested("learning")
        }
        SettingsCategoryRow {
            title: qsTranslate("SettingsPopup", "Capture & popups")
            summary: qsTranslate("SettingsPopup", "Trigger channels, clipboard, popup frequency")
            onPicked: root.categoryRequested("capture")
        }
        SettingsCategoryRow {
            title: qsTranslate("SettingsPopup", "Model service")
            summary: qsTranslate("SettingsPopup", "Provider, model, API configuration")
            onPicked: root.categoryRequested("model")
        }
        SettingsCategoryRow {
            title: qsTranslate("SettingsPopup", "Daily budget")
            summary: qsTranslate("SettingsPopup", "Limit model spending for one local day")
            onPicked: root.categoryRequested("budget")
        }
        SettingsCategoryRow {
            title: qsTranslate("SettingsPopup", "Extensions")
            summary: qsTranslate("SettingsPopup", "Desktop companion")
            onPicked: root.categoryRequested("extensions")
        }
    }
}
