pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: page

    property int themeChoice: Tokens.themeIndex(Controller.settings.theme)
    property var draftColors: ({})
    readonly property var contrast: Tokens.validateCustomTheme(draftColors)
    readonly property var openChildRect: themeField.openChildRect
    implicitHeight: content.implicitHeight

    function closeChild() {
        themeField.closeList();
    }

    function loadDraft() {
        page.draftColors = Tokens.colorsForTheme(Controller.settings.theme);
    }

    function updateDraft(role, value) {
        const next = {};
        for (const key in page.draftColors)
            next[key] = page.draftColors[key];
        next[role] = value;
        page.draftColors = next;
    }

    function chooseTheme(index) {
        page.themeChoice = index;
        if (index < 3)
            Controller.setTheme(Tokens.themeName(index));
        else
            page.loadDraft();
    }

    function applyCustomTheme() {
        if (page.contrast.valid)
            Controller.setTheme(Tokens.customThemeValue(page.draftColors));
    }

    Component.onCompleted: page.loadDraft()

    Connections {
        target: Controller
        function onSettingsChanged() {
            page.themeChoice = Tokens.themeIndex(Controller.settings.theme);
            if (page.themeChoice === 3)
                page.loadDraft();
        }
    }

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
            // A dropdown rather than a segment: the Custom path is a mode with an editor of its
            // own, so the row reads better as one closed choice than as four equal cells.
            DropdownField {
                id: themeField
                objectName: "themeField"
                width: parent.width
                options: [
                    {value: 0, label: qsTranslate("SettingsPopup", "Light"), group: "", note: ""},
                    {value: 1, label: qsTranslate("SettingsPopup", "Dark"), group: "", note: ""},
                    {value: 2, label: qsTranslate("SettingsPopup", "Forest"), group: "", note: ""},
                    {value: 3, label: qsTranslate("SettingsPopup", "Custom"), group: "", note: ""}
                ]
                currentValue: page.themeChoice
                onPicked: (value) => page.chooseTheme(value)
            }
        }

        Column {
            width: parent.width
            spacing: 10
            visible: page.themeChoice === 3
            height: visible ? implicitHeight : 0

            Text {
                text: qsTranslate("SettingsPopup", "Custom semantic colors")
                color: Tokens.text
                font.pixelSize: 13
                font.weight: Font.Bold
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTranslate("SettingsPopup", "Use six-digit hex colors. Body, muted, and faint text must each pass WCAG AA on the panel.")
                color: Tokens.muted
                font.pixelSize: 11
            }

            Repeater {
                model: [
                    {role: "bg", label: qsTranslate("SettingsPopup", "Background")},
                    {role: "panel", label: qsTranslate("SettingsPopup", "Panel")},
                    {role: "text", label: qsTranslate("SettingsPopup", "Body text")},
                    {role: "muted", label: qsTranslate("SettingsPopup", "Muted text")},
                    {role: "faint", label: qsTranslate("SettingsPopup", "Faint text")},
                    {role: "ok", label: qsTranslate("SettingsPopup", "Accent")}
                ]

                delegate: Column {
                    id: colorField
                    required property var modelData
                    width: content.width
                    spacing: 5

                    Text {
                        text: colorField.modelData.label
                        color: Tokens.muted
                        font.pixelSize: 12
                    }

                    Rectangle {
                        width: parent.width
                        height: 35
                        radius: Tokens.radiusField
                        color: Tokens.panel2
                        border.width: colorInput.activeFocus ? 2 : 1
                        border.color: colorInput.activeFocus ? Tokens.ok : Tokens.line

                        Rectangle {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 19
                            height: 19
                            radius: 5
                            color: Tokens.validHex(page.draftColors[colorField.modelData.role])
                                   ? page.draftColors[colorField.modelData.role] : Tokens.panel
                            border.width: 1
                            border.color: Tokens.line
                        }

                        TextInput {
                            id: colorInput
                            anchors.left: parent.left
                            anchors.leftMargin: 37
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.draftColors[colorField.modelData.role] || ""
                            color: Tokens.text
                            font.pixelSize: 13
                            selectByMouse: true
                            activeFocusOnTab: true
                            onTextEdited: page.updateDraft(colorField.modelData.role, text)
                        }
                    }
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: page.contrast.valid
                      ? qsTranslate("SettingsPopup", "AA contrast passes: body %1, muted %2, faint %3")
                        .arg(page.contrast.bodyRatio.toFixed(2))
                        .arg(page.contrast.mutedRatio.toFixed(2))
                        .arg(page.contrast.faintRatio.toFixed(2))
                      : qsTranslate("SettingsPopup", "AA contrast requires valid colors and a 4.5:1 ratio for body, muted, and faint text.")
                color: page.contrast.valid ? Tokens.ok : Tokens.danger
                font.pixelSize: 11
            }

            Rectangle {
                width: parent.width
                height: 35
                radius: Tokens.radiusPill
                color: page.contrast.valid ? Tokens.ink : Tokens.panel2
                opacity: page.contrast.valid ? 1 : 0.58

                Text {
                    anchors.centerIn: parent
                    text: qsTranslate("SettingsPopup", "Apply custom colors")
                    color: page.contrast.valid ? Tokens.on : Tokens.faint
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                TapHandler {
                    enabled: page.contrast.valid
                    onTapped: page.applyCustomTheme()
                }
            }
        }

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Launch at startup")
            checked: Controller.settings.autostart
            onToggled: (on) => Controller.setAutostart(on)
        }
    }
}
