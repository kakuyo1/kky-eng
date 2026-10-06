import QtQuick

Item {
    id: root

    property var settings: Controller.settings
    property real dailyBudget: Number(settings.dailyBudget || 0)
    signal budgetChanged(real amount)

    readonly property var openChildRect: null
    implicitHeight: content.implicitHeight

    function restoreText() {
        amountInput.text = root.dailyBudget > 0 ? root.dailyBudget.toFixed(2) : "0"
    }

    function closeChild() {}

    Column {
        id: content
        width: parent.width
        spacing: 16

        Column {
            width: parent.width
            spacing: 7

            Text {
                text: qsTranslate("SettingsPopup", "Daily budget")
                color: Tokens.muted
                font.pixelSize: 12
            }

            Rectangle {
                width: parent.width
                height: 35
                radius: Tokens.radiusField
                color: Tokens.panel2
                border.width: 1
                border.color: amountInput.activeFocus ? Tokens.ink : Tokens.line

                TextInput {
                    id: amountInput
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    verticalAlignment: TextInput.AlignVCenter
                    text: root.dailyBudget > 0 ? root.dailyBudget.toFixed(2) : "0"
                    color: Tokens.text
                    font.pixelSize: 13
                    selectByMouse: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    validator: DoubleValidator { bottom: 0; top: 1000000000; decimals: 2 }

                    onEditingFinished: {
                        const amount = Number(text)
                        if (!isFinite(amount) || amount < 0) {
                            root.restoreText()
                            return
                        }
                        root.budgetChanged(amount)
                    }
                }
            }

            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "Set 0 to pause the cap.")
                color: Tokens.faint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }
}
