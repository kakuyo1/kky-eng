import QtQuick
import QtQuick.Window

/**
 * The tray menu (UI.md section 4.2), drawn rather than handed to QMenu.
 *
 * A surface like the others: a frameless window with a card in it. The language list is a
 * second card that unfolds to the left of the row it belongs to, which is why the window is
 * wider than the menu card -- the extra strip stays empty, and the window keeps the same width
 * whether the list is out or not, so nothing jumps when it appears.
 */
Window {
    id: menu

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int padding: 6
    readonly property int cardWidth: 240
    readonly property int listWidth: 104
    readonly property int gap: 8

    /// Where the menu card's edges sit inside the window: the list takes the left strip.
    readonly property int cardLeft: shadowMargin + listWidth + gap
    readonly property int listLeft: cardLeft - gap - listWidth

    width: cardLeft + cardWidth + shadowMargin
    height: body.implicitHeight + 2 * padding + 2 * shadowMargin

    /// True while a selection is captured at all; the state line's dot and label read it.
    readonly property bool capturing: controller.settings.selectionCapture

    /// The interface language, and the name to show for it in the list.
    readonly property string language: controller.settings.uiLanguage
    readonly property string languageName: language === "zh" ? "中文" : "English"

    /// Whether the language list is unfolded.
    property bool listVisible: false

    /// The day's tally for the statistics row, in the shape the tray tooltip uses.
    readonly property string todayFigures: controller.stats.todayPops + " " + qsTr("words")
        + " · " + controller.stats.currency + controller.stats.todayCost.toFixed(2)

    /// What counts as a press on this surface, in screen coordinates: the card, plus the
    /// language list while it is out. main.qml's outside-press rule reads it.
    readonly property int hitLeft: listVisible ? menu.x + listLeft : menu.x + cardLeft
    readonly property int hitRight: menu.x + cardLeft + cardWidth
    readonly property rect hitRect: {
        const top = menu.y + shadowMargin;
        const ownBottom = top + menu.height - 2 * shadowMargin;
        const listBottom = menu.y + languageList.y + languageList.height - shadowMargin;
        const bottom = listVisible ? Math.max(ownBottom, listBottom) : ownBottom;
        return Qt.rect(hitLeft, top, hitRight - hitLeft, bottom - top);
    }

    /**
     * Put the menu up beside the tray icon.
     * @param iconRect The icon's geometry, in the screen coordinates the shell reports.
     */
    function openAt(iconRect) {
        // Right-aligned with the icon and standing on top of it, which is where a menu on a
        // bottom-edge taskbar belongs; flipped under the icon when the screen's top is too near.
        menu.x = iconRect.x + iconRect.width - cardLeft - cardWidth;
        const above = iconRect.y - gap - height;
        menu.y = above >= screen.virtualY ? above : iconRect.y + iconRect.height + gap;
        listVisible = false;
        visible = true;
    }

    ShadowCard {
        id: card
        x: menu.cardLeft - menu.shadowMargin
        y: 0
        width: menu.cardWidth + 2 * menu.shadowMargin
        height: menu.height
        radius: Tokens.radiusMenu

        Column {
            id: body
            x: menu.padding
            y: menu.padding
            width: menu.cardWidth - 2 * menu.padding
            spacing: 0

            // The state line: the dot says whether a selection is captured at all, the text
            // names what the app is doing.
            Item {
                width: parent.width
                height: 34

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Tokens.line2
                }

                Rectangle {
                    id: dotRing
                    anchors.left: parent.left
                    anchors.leftMargin: 11
                    anchors.verticalCenter: parent.verticalCenter
                    width: 13
                    height: 13
                    radius: Tokens.radiusPill
                    color: menu.capturing ? Tokens.okBg : Tokens.panel2
                }
                Rectangle {
                    anchors.centerIn: dotRing
                    width: 7
                    height: 7
                    radius: Tokens.radiusPill
                    color: menu.capturing ? Tokens.ok : Tokens.faint
                }
                Text {
                    anchors.left: dotRing.right
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: menu.capturing ? controller.modeLabel : qsTr("Selection capture is off")
                    color: Tokens.text
                    font.family: Tokens.fontFamily
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }

            Item { width: 1; height: 4 }

            MenuRow {
                id: statsRow
                width: parent.width
                icon: "M5 19V9M12 19V5M19 19v-6"
                label: qsTr("Today's statistics")
                note: menu.todayFigures
                onPicked: {
                    menu.visible = false;
                    root.showStats();
                }
            }

            MenuRow {
                id: languageRow
                width: parent.width
                icon: "M20 12a8 8 0 1 1-16 0a8 8 0 1 1 16 0M4 12h16M12 4c2.6 2.8 2.6 13.2 0 16M12 4c-2.6 2.8-2.6 13.2 0 16"
                label: qsTr("Language")
                note: menu.languageName

                // The list unfolds on hover and folds again a moment after the pointer leaves.
                // The moment is what lets the pointer cross the gap between the two cards.
                onHoveredChanged: {
                    if (hovered) {
                        listHide.stop();
                        menu.listVisible = true;
                    } else {
                        listHide.restart();
                    }
                }
            }

            MenuRow {
                width: parent.width
                icon: "M4 7h16M4 12h16M4 17h16M11 7h0.01M15 12h0.01M8.5 17h0.01"
                label: qsTr("Settings")
                onPicked: {
                    menu.visible = false;
                    root.showSettings();
                }
            }

            Rectangle {
                x: 2
                width: parent.width - 4
                height: 1
                color: Tokens.line2
                anchors.margins: 5
            }

            MenuRow {
                width: parent.width
                icon: "M12 3v8M6.3 5.8a8 8 0 1 0 11.4 0"
                label: qsTr("Quit")
                danger: true
                onPicked: Qt.quit()
            }
        }
    }

    ShadowCard {
        id: languageList
        x: menu.listLeft - menu.shadowMargin
        y: menu.shadowMargin + menu.padding + languageRow.y - 6
        width: menu.listWidth + 2 * menu.shadowMargin
        height: listColumn.implicitHeight + 10 + 2 * menu.shadowMargin
        visible: menu.listVisible
        radius: Tokens.radiusGroup

        Column {
            id: listColumn
            x: 5
            y: 5
            width: menu.listWidth - 10

            Repeater {
                model: [{ code: "zh", name: "中文" }, { code: "en", name: "English" }]

                Rectangle {
                    required property var modelData

                    width: listColumn.width
                    height: 28
                    radius: 8
                    color: modelData.code === menu.language ? Tokens.ink
                                                            : (rowHover.hovered ? Tokens.panel2 : "transparent")

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 11
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name
                        color: modelData.code === menu.language ? Tokens.on : Tokens.text
                        font.family: Tokens.fontFamily
                        font.pixelSize: 13
                    }

                    HoverHandler {
                        id: rowHover
                        cursorShape: Qt.PointingHandCursor
                        onHoveredChanged: if (hovered) listHide.stop()
                    }
                    TapHandler {
                        onTapped: {
                            controller.setUiLanguage(modelData.code);
                            menu.visible = false;
                        }
                    }
                }
            }
        }
    }

    Timer {
        id: listHide
        interval: 160
        onTriggered: menu.listVisible = false
    }
}
