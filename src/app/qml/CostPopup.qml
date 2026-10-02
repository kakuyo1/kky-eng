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

    /// The reader asked for the panel this one was opened from.
    signal backRequested()

    ShadowCard {
        anchors.fill: parent
        movable: true

        Column {
            id: column
            x: 20
            y: 18
            width: cost.cardWidth - 40
            spacing: 0

            // The icons are anchored to the right edge rather than pushed there by a spacer:
            // a spacer sized around the English title lands the glyphs past the card the
            // moment the title is a different width, which is every translation of it.
            Item {
                width: parent.width
                height: title.height

                Text {
                    id: title
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Cost")
                    color: Tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    // Back to the statistics panel, which is where this one opens from. This
                    // panel replaces it rather than stacking on it, so the way up has to be
                    // visible.
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/ui-back.svg"
                        color: Tokens.faint
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: cost.backRequested() }
                    }
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/ui-close.svg"
                        color: Tokens.faint
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: cost.visible = false }
                    }
                }
            }

            Text {
                topPadding: 16
                text: controller.cost.currency + controller.cost.month.toFixed(2)
                color: Tokens.text
                font.family: Tokens.monoFamily
                font.pixelSize: 32
                font.weight: Font.Bold
            }

            Text {
                topPadding: 4
                text: qsTr("Spent this month")
                color: Tokens.faint
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
