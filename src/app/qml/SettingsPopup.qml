import QtQuick
import QtQuick.Window

/**
 * The settings panel (UI.md section 4.4).
 *
 * Every control applies as it is changed; only the API key waits for Save, because a
 * half-typed key is not a key. The three capture switches keep OCR and automatic scanning
 * visible but inert: they are phase-1 placeholders, and a surface that greys a control
 * without a word of explanation is what PHASE1.md section 2 asks for.
 */
Window {
    id: settings

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 360

    /// The card of a window this panel opened, in screen coordinates, or null. main.qml's
    /// outside-press rule reads it: a press that lands on the level list belongs to the list.
    readonly property var openChildRect: levelField.openChildRect

    /// The list outlives this panel otherwise: it is a window of its own, and nothing else
    /// knows the panel has gone.
    onVisibleChanged: if (!visible) levelField.closeList()

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 36 + 2 * shadowMargin

    /// Put the panel up beside the tray icon, or at the screen's bottom-right corner when the
    /// shell does not say where the icon is.
    function openNear(anchor) {
        settings.x = Math.max(8, anchor.x - width + 60);
        settings.y = Math.max(8, anchor.y - height);
        visible = true;
        apiField.text = "";
    }

    ShadowCard {
        anchors.fill: parent
        radius: Tokens.radiusCard

        Column {
            id: column
            x: 20
            y: 18
            width: settings.cardWidth - 40
            spacing: 15

            Row {
                width: parent.width
                Text {
                    text: qsTr("Settings")
                    color: Tokens.text
                    font.family: Tokens.fontFamily
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Item { width: parent.width - 40; height: 1 }
                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    path: "M5 5l14 14M19 5L5 19"
                    color: Tokens.faint
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: settings.visible = false }
                }
            }

            Column {
                width: parent.width
                spacing: 7
                Text {
                    text: qsTr("Vocabulary level")
                    color: Tokens.muted
                    font.family: Tokens.fontFamily
                    font.pixelSize: 12
                }
                DropdownField {
                    id: levelField
                    width: parent.width
                    options: controller.settings.levels
                    currentValue: controller.settings.level
                    onPicked: (value) => controller.setLevel(value)
                }
            }

            Column {
                width: parent.width
                spacing: 7
                Text {
                    text: qsTr("Explanation language")
                    color: Tokens.muted
                    font.family: Tokens.fontFamily
                    font.pixelSize: 12
                }
                Segment {
                    width: parent.width
                    height: 31
                    labels: [qsTr("English"), qsTr("中文")]
                    currentIndex: controller.settings.explanationLang === "zh" ? 1 : 0
                    onPicked: (index) => controller.setExplanationLang(index === 1 ? "zh" : "en")
                }
            }

            Column {
                width: parent.width
                spacing: 7
                Text {
                    text: qsTr("Theme")
                    color: Tokens.muted
                    font.family: Tokens.fontFamily
                    font.pixelSize: 12
                }
                Segment {
                    width: parent.width
                    height: 31
                    labels: [qsTr("Light"), qsTr("Dark")]
                    currentIndex: controller.settings.theme === "dark" ? 1 : 0
                    onPicked: (index) => controller.setTheme(index === 1 ? "dark" : "light")
                }
            }

            Column {
                width: parent.width
                spacing: 7
                Text {
                    text: qsTr("API")
                    color: Tokens.muted
                    font.family: Tokens.fontFamily
                    font.pixelSize: 12
                }
                Rectangle {
                    width: parent.width
                    height: 34
                    radius: Tokens.radiusField
                    color: Tokens.panel2
                    border.width: 1
                    border.color: Tokens.line

                    TextInput {
                        id: apiField
                        anchors.fill: parent
                        anchors.leftMargin: 11
                        anchors.rightMargin: 11
                        verticalAlignment: TextInput.AlignVCenter
                        echoMode: TextInput.Password
                        color: Tokens.text
                        font.family: Tokens.fontFamily
                        font.pixelSize: 13
                        selectByMouse: true
                        // The stored key is never read back, so the field starts empty and
                        // shows the prototype's placeholder instead of a masked copy of it.
                        Text {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            visible: apiField.text.length === 0
                            text: controller.settings.hasApiKey ? "sk-" + "•".repeat(28) : "sk-********************************"
                            color: Tokens.faint
                            font: apiField.font
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 7
                Text {
                    text: qsTr("Capture")
                    color: Tokens.muted
                    font.family: Tokens.fontFamily
                    font.pixelSize: 12
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
                            width: parent.width
                            label: qsTr("Selection")
                            checked: controller.settings.selectionCapture
                            onToggled: (on) => controller.setSelectionCapture(on)
                        }
                        SwitchRow {
                            width: parent.width
                            label: qsTr("OCR")
                            checked: false
                            interactive: false
                        }
                        SwitchRow {
                            width: parent.width
                            label: qsTr("Auto scan")
                            checked: false
                            interactive: false
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 7
                Text {
                    text: qsTr("Global hotkey")
                    color: Tokens.muted
                    font.family: Tokens.fontFamily
                    font.pixelSize: 12
                }
                Rectangle {
                    width: parent.width
                    height: 34
                    radius: Tokens.radiusField
                    color: Tokens.panel2
                    border.width: 1
                    border.color: Tokens.line
                    opacity: 0.45

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 11
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Toggle auto scan")
                        color: Tokens.faint
                        font.family: Tokens.fontFamily
                        font.pixelSize: 13
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 30
                        height: 21
                        radius: 6
                        color: Tokens.panel
                        border.width: 1
                        border.color: Tokens.line
                        Text {
                            anchors.centerIn: parent
                            text: "F8"
                            color: Tokens.muted
                            font.family: Tokens.fontFamily
                            font.pixelSize: 11
                        }
                    }
                }
            }

            Row {
                width: parent.width
                spacing: 8
                layoutDirection: Qt.RightToLeft

                Rectangle {
                    width: saveLabel.width + 26
                    height: 27
                    radius: Tokens.radiusPill
                    color: Tokens.ink
                    Text {
                        id: saveLabel
                        anchors.centerIn: parent
                        text: qsTr("Save")
                        color: Tokens.on
                        font.family: Tokens.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            if (apiField.text.length > 0)
                                controller.setApiKey(apiField.text);
                            settings.visible = false;
                        }
                    }
                }

                Rectangle {
                    width: resetLabel.width + 26
                    height: 27
                    radius: Tokens.radiusPill
                    color: Tokens.panel2
                    border.width: 1
                    border.color: Tokens.line
                    Text {
                        id: resetLabel
                        anchors.centerIn: parent
                        text: qsTr("Restore defaults")
                        color: Tokens.muted
                        font.family: Tokens.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            controller.setLevel(2);
                            controller.setExplanationLang("en");
                            controller.setTheme("light");
                        }
                    }
                }
            }
        }
    }
}
