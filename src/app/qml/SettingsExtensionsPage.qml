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
            checked: Pet.enabled
            interactive: Pet.available
            onToggled: (on) => Pet.setEnabled(on)
        }

        SwitchRow {
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
    }
}
