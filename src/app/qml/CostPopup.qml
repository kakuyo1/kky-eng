import QtQuick
import QtQuick.Window

/**
 * What the explanations have cost (UI.md section 4.8): the month in the hero, four figures
 * underneath. Same shape as the statistics panel, a different dimension.
 *
 * The three rows the controller buckets carry the tokens their amount was priced from as well as
 * the amount: the count in StatRow's quieter `note` slot, the amount in `value`, so the four
 * amounts keep one right edge. The daily average is a derived figure rather than a bucket, so
 * the controller prices no token count for it and the row carries the amount alone.
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
                text: Controller.cost.currency + Controller.cost.month.toFixed(2)
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

            // The token count goes in the note slot rather than in front of the amount in
            // `value`: the amount keeps the bold right-aligned face and the four of them keep
            // one right edge, and the count is quiet enough to sit there at 11px. String(), not
            // the raw number, is the house way of spelling an integer.
            StatRow {
                width: parent.width
                label: qsTr("Today")
                note: qsTr("%1 tokens").arg(String(Controller.cost.todayTokens))
                value: Controller.cost.currency + Controller.cost.today.toFixed(2)
            }
            StatRow {
                width: parent.width
                label: qsTr("Yesterday")
                note: qsTr("%1 tokens").arg(String(Controller.cost.yesterdayTokens))
                value: Controller.cost.currency + Controller.cost.yesterday.toFixed(2)
            }
            StatRow {
                width: parent.width
                label: qsTr("This week")
                note: qsTr("%1 tokens").arg(String(Controller.cost.weekTokens))
                value: Controller.cost.currency + Controller.cost.week.toFixed(2)
            }
            StatRow {
                width: parent.width
                label: qsTr("Daily average")
                // A derived average, not a bucket: cost() prices no token count for it, so the
                // row carries the amount alone rather than a figure invented here.
                value: Controller.cost.currency + Controller.cost.dailyAverage.toFixed(2)
            }
        }
    }
}
