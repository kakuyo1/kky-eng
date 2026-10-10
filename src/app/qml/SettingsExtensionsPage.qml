pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: page

    readonly property var openChildRect: null
    implicitHeight: content.implicitHeight

    function closeChild() {}

    function actionLabel(name) {
        switch (name) {
        case "idle": return qsTranslate("SettingsPopup", "Idle");
        case "study": return qsTranslate("SettingsPopup", "Reading");
        case "thinking": return qsTranslate("SettingsPopup", "Thinking");
        case "celebrate": return qsTranslate("SettingsPopup", "Celebrate");
        case "encourage": return qsTranslate("SettingsPopup", "Encourage");
        case "sleep": return qsTranslate("SettingsPopup", "Sleep");
        case "click_react": return qsTranslate("SettingsPopup", "Poked");
        case "pickup": return qsTranslate("SettingsPopup", "Picked up");
        case "look_around": return qsTranslate("SettingsPopup", "Look around");
        case "yawn": return qsTranslate("SettingsPopup", "Yawn");
        case "stretch": return qsTranslate("SettingsPopup", "Stretch");
        default: return name;
        }
    }

    function accessoryLabel(id) {
        switch (id) {
        case "hat": return qsTranslate("SettingsPopup", "Top hat");
        case "beanie": return qsTranslate("SettingsPopup", "Beanie");
        case "sprout": return qsTranslate("SettingsPopup", "Sprout");
        case "crown": return qsTranslate("SettingsPopup", "Crown");
        case "glasses": return qsTranslate("SettingsPopup", "Round glasses");
        case "sunglasses": return qsTranslate("SettingsPopup", "Sunglasses");
        case "bowtie": return qsTranslate("SettingsPopup", "Bow tie");
        default: return id;
        }
    }

    function slotLabel(slot) {
        switch (slot) {
        case "head": return qsTranslate("SettingsPopup", "Head");
        case "face": return qsTranslate("SettingsPopup", "Face");
        default: return qsTranslate("SettingsPopup", "Body");
        }
    }

    Column {
        id: content
        width: parent.width
        spacing: 7

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Desktop companion")
            checked: Pet.enabled
            interactive: Pet.available
            onToggled: (on) => Pet.setEnabled(on)
        }

        SwitchRow {
            objectName: "passthroughSwitch"
            width: parent.width
            label: qsTranslate("SettingsPopup", "Mouse pass-through")
            checked: Pet.passthrough
            interactive: Pet.enabled
            onToggled: (on) => Pet.setPassthrough(on)
        }

        Row {
            id: sizeRow

            width: parent.width
            height: 33
            spacing: 8

            Text {
                id: sizeLabel
                anchors.verticalCenter: parent.verticalCenter
                text: qsTranslate("SettingsPopup", "Desktop pet size")
                color: Pet.enabled ? Tokens.text : Tokens.faint
                font.pixelSize: 13
            }

            Item {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, sizeRow.width - sizeLabel.width - sizeSlider.width - sizeValue.width - 3 * sizeRow.spacing)
                height: 1
            }

            StepSlider {
                id: sizeSlider
                anchors.verticalCenter: parent.verticalCenter
                from: Pet.minScale
                to: Pet.maxScale
                value: Pet.scale
                interactive: Pet.enabled
                onMoved: (stop) => Pet.setScale(stop)
            }

            Text {
                id: sizeValue
                anchors.verticalCenter: parent.verticalCenter
                text: Pet.scale + "×"
                color: Pet.enabled ? Tokens.muted : Tokens.faint
                font.pixelSize: 13
            }
        }

        // Everything below exists only while the pet is on (PHASE3 3.5): the pet type, the preview, the actions and
        // the accessories. Off, the whole block is gone and no pet animation runs.
        Column {
            id: petSection

            width: parent.width
            spacing: 14
            visible: Pet.enabled

            Column {
                width: parent.width
                spacing: 7

                Text {
                    text: qsTranslate("SettingsPopup", "Pet type")
                    color: Tokens.muted
                    font.pixelSize: 11
                }

                DropdownField {
                    width: parent.width
                    options: [{value: "dog", label: qsTranslate("SettingsPopup", "White dog"), group: "", note: ""}]
                    currentValue: "dog"
                }
            }

            Column {
                width: parent.width
                spacing: 7

                Text {
                    text: qsTranslate("SettingsPopup", "Preview")
                    color: Tokens.muted
                    font.pixelSize: 11
                }

                PetStage {
                    width: parent.width
                }
            }

            Column {
                width: parent.width
                spacing: 7

                Text {
                    text: qsTranslate("SettingsPopup", "Actions")
                    color: Tokens.muted
                    font.pixelSize: 11
                }

                Flow {
                    width: parent.width
                    spacing: 6

                    Repeater {
                        model: Pet.actions

                        Chip {
                            required property string modelData

                            label: page.actionLabel(modelData)
                            selected: Pet.action === modelData
                            onPicked: Pet.preview(modelData)
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 7

                Text {
                    text: qsTranslate("SettingsPopup", "Accessories")
                    color: Tokens.muted
                    font.pixelSize: 11
                }

                Repeater {
                    model: ["head", "face", "body"]

                    Row {
                        id: slotRow

                        required property string modelData

                        width: petSection.width
                        spacing: 10

                        Text {
                            width: 52
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.slotLabel(slotRow.modelData)
                            color: Tokens.faint
                            font.pixelSize: 11
                        }

                        Flow {
                            width: slotRow.width - 62
                            spacing: 6

                            Repeater {
                                model: Pet.accessories.filter(item => item.slot === slotRow.modelData)

                                Chip {
                                    required property var modelData

                                    label: page.accessoryLabel(modelData.id)
                                    selected: Pet.wornIn[slotRow.modelData] === modelData.id
                                    onPicked: Pet.toggleAccessory(modelData.id)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
