import QtQuick
import QtQuick.Window

/**
 * Today's statistics (UI.md section 4.6).
 *
 * Two rows drill down: the cost row opens the cost popup and the all-time row opens the
 * words popup. The numbers come straight off the controller, which does the date arithmetic.
 */
Window {
    id: stats

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 280

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 34 + 2 * shadowMargin

    signal costRequested()
    signal wordsRequested()

    function openNear(anchor) {
        stats.x = Math.max(8, anchor.x - width + 60);
        stats.y = Math.max(8, anchor.y - height);
        visible = true;
    }

    ShadowCard {
        anchors.fill: parent

        Column {
            id: column
            x: 20
            y: 18
            width: stats.cardWidth - 40
            spacing: 0

            Row {
                width: parent.width
                Text {
                    text: qsTr("Today")
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
                    TapHandler { onTapped: stats.visible = false }
                }
            }

            Text {
                topPadding: 16
                text: String(controller.stats.todayPops)
                color: Tokens.text
                font.family: Tokens.monoFamily
                font.pixelSize: 32
                font.weight: Font.DemiBold
            }

            Text {
                topPadding: 4
                text: qsTr("Words explained today")
                color: Tokens.faint
                font.family: Tokens.fontFamily
                font.pixelSize: 12
            }

            Item { width: 1; height: 14 }
            Rectangle { width: parent.width; height: 1; color: Tokens.line2 }
            Item { width: 1; height: 6 }

            StatRow {
                width: parent.width
                label: qsTr("Known")
                value: String(controller.stats.todayLearned)
            }
            StatRow {
                width: parent.width
                label: qsTr("New")
                value: String(controller.stats.todayFresh)
            }
            StatRow {
                width: parent.width
                label: qsTr("Cost")
                value: controller.stats.currency + controller.stats.todayCost.toFixed(2)
                clickable: true
                onTapped: stats.costRequested()
            }
            StatRow {
                width: parent.width
                label: qsTr("All time")
                value: qsTr("%1 words").arg(controller.stats.historyTotal)
                clickable: true
                onTapped: stats.wordsRequested()
            }
        }
    }
}
