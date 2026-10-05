import QtQuick

Item {
    readonly property var openChildRect: null
    implicitHeight: content.implicitHeight

    function closeChild() {}

    Column {
        id: content
        width: parent.width
        spacing: 7

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Desktop companion")
            checked: false
            interactive: false
        }
    }
}
