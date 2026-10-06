import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root

    signal childRequested(string route)
    readonly property var openChildRect: lengthField.openChildRect
    implicitHeight: content.implicitHeight

    function closeChild() { lengthField.closeList(); }

    Column {
        id: content
        width: parent.width
        spacing: 14

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Trigger channels")
                color: Tokens.muted
                font.pixelSize: 11
            }
            Rectangle {
                width: parent.width
                implicitHeight: switches.implicitHeight + 8
                radius: Tokens.radiusGroup
                color: Tokens.panel2
                border.width: 1
                border.color: Tokens.line

                Column {
                    id: switches
                    x: 12
                    y: 4
                    width: parent.width - 24
                    spacing: 0

                    SwitchRow {
                        objectName: "selectionCapture"
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "Selection")
                        checked: Controller.settings.selectionCapture
                        onToggled: (on) => Controller.setSelectionCapture(on)
                    }
                    SwitchRow {
                        objectName: "ocrCapture"
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "OCR")
                        checked: Controller.settings.ocrCapture
                        interactive: Controller.settings.ocrAvailable
                        onToggled: (on) => Controller.setOcrCapture(on)
                    }
                    SwitchRow {
                        objectName: "autoScan"
                        width: parent.width
                        label: qsTranslate("SettingsPopup", "Auto scan")
                        checked: Controller.settings.autoScan
                        interactive: Controller.settings.ocrAvailable
                        onToggled: (on) => Controller.setAutoScan(on)
                    }
                }
            }
        }

        SettingsLinkRow {
            label: qsTranslate("SettingsPopup", "Popup & clipboard")
            note: Controller.settings.clipboardPolicy === "silent"
                  ? qsTranslate("SettingsPopup", "Give up silently")
                  : qsTranslate("SettingsPopup", "Raise to top")
            onPicked: root.childRequested("clipboard")
        }

        Text {
            width: parent.width
            visible: !Controller.settings.ocrAvailable
            text: Controller.settings.ocrStatus === "checking" ? qsTr("Checking OCR")
                  : Controller.settings.ocrStatus === "english-data-missing" ? qsTr("English OCR data is missing")
                  : qsTr("OCR runtime is unavailable")
            color: Tokens.muted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        // The bundled runtime is what the installer lays down; these two are for a reader who
        // already keeps their own Tesseract and would rather point at it.
        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTr("Tesseract executable"); color: Tokens.muted; font.pixelSize: 12 }
            TextField {
                id: tesseractField
                objectName: "tesseractExecutable"
                width: parent.width
                text: Controller.settings.tesseractExecutable
                font.pixelSize: 12
                color: Tokens.text
                selectByMouse: true
                maximumLength: 260
                Accessible.name: qsTr("Tesseract executable")
                background: Rectangle {
                    radius: Tokens.radiusField
                    color: Tokens.panel2
                    border.width: 1
                    border.color: tesseractField.activeFocus ? Tokens.ink : Tokens.line
                }
                onEditingFinished: {
                    Controller.setTesseractExecutable(text);
                    text = Qt.binding(() => Controller.settings.tesseractExecutable);
                }
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTr("Tesseract data folder"); color: Tokens.muted; font.pixelSize: 12 }
            TextField {
                id: tesseractDataField
                objectName: "tesseractDataDirectory"
                width: parent.width
                text: Controller.settings.tesseractDataDirectory
                font.pixelSize: 12
                color: Tokens.text
                selectByMouse: true
                maximumLength: 260
                Accessible.name: qsTr("Tesseract data folder")
                background: Rectangle {
                    radius: Tokens.radiusField
                    color: Tokens.panel2
                    border.width: 1
                    border.color: tesseractDataField.activeFocus ? Tokens.ink : Tokens.line
                }
                onEditingFinished: {
                    Controller.setTesseractDataDirectory(text);
                    text = Qt.binding(() => Controller.settings.tesseractDataDirectory);
                }
            }
        }

        Text {
            width: parent.width
            text: qsTr("Leave both empty to use the bundled Tesseract.")
            color: Tokens.faint
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTr("Minimum word length"); color: Tokens.muted; font.pixelSize: 12 }
            DropdownField {
                id: lengthField
                objectName: "minimumWordLength"
                width: parent.width
                options: [
                    {value: 2, label: "2", group: "", note: ""},
                    {value: 3, label: "3", group: "", note: ""},
                    {value: 4, label: "4", group: "", note: ""},
                    {value: 5, label: "5", group: "", note: ""}
                ]
                currentValue: Controller.settings.minimumWordLength
                onPicked: (value) => Controller.setMinimumWordLength(value)
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTr("Drag threshold"); color: Tokens.muted; font.pixelSize: 12 }
            Segment {
                objectName: "dragSensitivity"
                width: parent.width
                height: 31
                labels: [qsTr("Sensitive"), qsTr("Standard"), qsTr("Reluctant")]
                currentIndex: Controller.settings.dragSensitivity === "sensitive" ? 0
                              : Controller.settings.dragSensitivity === "reluctant" ? 2 : 1
                onPicked: (index) => Controller.setDragSensitivity(["sensitive", "standard", "reluctant"][index])
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text { text: qsTr("Scan whitelist"); color: Tokens.muted; font.pixelSize: 12 }
            TextField {
                id: whitelistField
                objectName: "scanWhitelist"
                width: parent.width
                text: Controller.settings.scanWhitelist
                font.pixelSize: 13
                color: Tokens.text
                selectByMouse: true
                maximumLength: 4096
                Accessible.name: qsTr("Scan whitelist")
                background: Rectangle {
                    radius: Tokens.radiusField
                    color: Tokens.panel2
                    border.width: 1
                    border.color: whitelistField.activeFocus ? Tokens.ink : Tokens.line
                }
                onEditingFinished: {
                    Controller.setScanWhitelist(text);
                    text = Qt.binding(() => Controller.settings.scanWhitelist);
                }
            }
        }
    }
}
