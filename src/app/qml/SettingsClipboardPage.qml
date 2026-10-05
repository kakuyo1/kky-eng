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
                text: qsTranslate("SettingsPopup", "When the clipboard is occupied")
                color: Tokens.muted
                font.pixelSize: 12
            }
            Segment {
                width: parent.width
                height: 31
                labels: [qsTranslate("SettingsPopup", "Raise to top"), qsTranslate("SettingsPopup", "Give up silently")]
                currentIndex: Controller.settings.clipboardPolicy === "silent" ? 1 : 0
                onPicked: (index) => Controller.setClipboardPolicy(index === 1 ? "silent" : "topmost")
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Popup frequency")
                color: Tokens.muted
                font.pixelSize: 12
            }
            Segment {
                width: parent.width
                height: 31
                labels: [qsTranslate("SettingsPopup", "Standard"), qsTranslate("SettingsPopup", "Less often")]
                currentIndex: Controller.settings.popupFrequency === "less" ? 1 : 0
                onPicked: (index) => Controller.setPopupFrequency(index === 1 ? "less" : "standard")
            }
        }
    }
}
