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

    /// @brief Take down the window this panel opened, if any.
    ///
    /// main.qml's outside-press rule reaches this through the panel rather than through the
    /// field: `levelField` is an id inside this file, and an id is not visible from another
    /// one. Naming it from there threw, and the throw took the rest of that rule -- the closing
    /// of the panels themselves -- down with it.
    function closeChild() {
        levelField.closeList();
    }

    /// The list outlives this panel otherwise: it is a window of its own, and nothing else
    /// knows the panel has gone. Coming up clears the key field, which is never read back.
    onVisibleChanged: {
        levelField.closeList();
        if (visible)
            apiField.text = "";
    }

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 36 + 2 * shadowMargin

    ShadowCard {
        anchors.fill: parent
        radius: Tokens.radiusCard
        movable: true

        // The level list is a window of its own and cannot follow the panel, so a drag takes
        // it down rather than leaving it behind.
        onDraggingChanged: if (dragging) levelField.closeList()

        Column {
            id: column
            x: 20
            y: 18
            width: settings.cardWidth - 40
            spacing: 15

            // The close icon is anchored to the right edge rather than pushed there by a
            // spacer: a spacer sized around the English title lands the glyph past the card
            // the moment the title is a different width, which is every translation of it.
            Item {
                width: parent.width
                height: title.height

                Text {
                    id: title
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Settings")
                    color: Tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }

                Icon {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/icons/ui-close.svg"
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
                    text: qsTr("Explanation language")
                    color: Tokens.muted
                    font.pixelSize: 12
                }
                Segment {
                    width: parent.width
                    height: 31
                    labels: [qsTr("English"), qsTr("中文")]
                    currentIndex: Controller.settings.explanationLang === "zh" ? 1 : 0
                    onPicked: (index) => Controller.setExplanationLang(index === 1 ? "zh" : "en")
                }
            }

            Column {
                width: parent.width
                spacing: 7
                Text {
                    text: qsTr("Theme")
                    color: Tokens.muted
                    font.pixelSize: 12
                }
                Segment {
                    width: parent.width
                    height: 31
                    labels: [qsTr("Light"), qsTr("Dark")]
                    currentIndex: Controller.settings.theme === "dark" ? 1 : 0
                    onPicked: (index) => Controller.setTheme(index === 1 ? "dark" : "light")
                }
            }

            Column {
                width: parent.width
                spacing: 7
                Text {
                    text: qsTr("API KEY")
                    color: Tokens.muted
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
                        font.pixelSize: 13
                        selectByMouse: true
                        // The stored key is never read back, so the field starts empty and
                        // shows the prototype's placeholder instead of a masked copy of it.
                        Text {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            visible: apiField.text.length === 0
                            text: Controller.settings.hasApiKey ? "sk-" + "•".repeat(28) : "sk-********************************"
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
                            checked: Controller.settings.selectionCapture
                            onToggled: (on) => Controller.setSelectionCapture(on)
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
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            if (apiField.text.length > 0)
                                Controller.setApiKey(apiField.text);
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
                        text: qsTr("Defaults")
                        color: Tokens.muted
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            Controller.setLevel(2);
                            Controller.setExplanationLang("en");
                            Controller.setTheme("light");
                        }
                    }
                }
            }
        }
    }
}
