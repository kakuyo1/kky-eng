import QtQuick
import QtQuick.Window

/**
 * What the explanations have cost (UI.md section 4.8): the month in the hero, four figures
 * underneath. Same shape as the statistics panel, a different dimension.
 */
Window {
    id: cost

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 280

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 34 + 2 * shadowMargin

    function openNear(anchor) {
        cost.x = Math.max(8, anchor.x - width + 60);
        cost.y = Math.max(8, anchor.y - height);
        visible = true;
    }

    ShadowCard {
        anchors.fill: parent

        Column {
            id: column
            x: 20
            y: 18
            width: cost.cardWidth - 40
            spacing: 0

            Row {
                width: parent.width
                Text {
                    text: qsTr("Cost")
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
                    TapHandler { onTapped: cost.visible = false }
                }
            }

            Text {
                topPadding: 16
                text: controller.cost.currency + controller.cost.month.toFixed(2)
                color: Tokens.text
                font.family: Tokens.monoFamily
                font.pixelSize: 32
                font.weight: Font.DemiBold
            }

            Text {
                topPadding: 4
                text: qsTr("Spent this month")
                color: Tokens.faint
                font.family: Tokens.fontFamily
                font.pixelSize: 12
            }

            Item { width: 1; height: 14 }
            Rectangle { width: parent.width; height: 1; color: Tokens.line2 }
            Item { width: 1; height: 6 }

            StatRow {
                width: parent.width
                label: qsTr("Today")
                value: controller.cost.currency + controller.cost.today.toFixed(2)
            }
            StatRow {
                width: parent.width
                label: qsTr("Yesterday")
                value: controller.cost.currency + controller.cost.yesterday.toFixed(2)
            }
            StatRow {
                width: parent.width
                label: qsTr("This week")
                value: controller.cost.currency + controller.cost.week.toFixed(2)
            }
            StatRow {
                width: parent.width
                label: qsTr("Daily average")
                value: controller.cost.currency + controller.cost.dailyAverage.toFixed(2)
            }
        }
    }
}
