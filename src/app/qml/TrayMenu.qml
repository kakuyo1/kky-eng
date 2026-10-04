pragma ComponentBehavior: Bound

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
    readonly property int languageRowOffset: 34 + 4

    /// Where the menu card's edges sit inside the window: the list takes the left strip.
    readonly property int cardLeft: shadowMargin + listWidth + gap
    readonly property int listLeft: cardLeft - gap - listWidth

    width: cardLeft + cardWidth + shadowMargin
    readonly property real bodyHeight: bodyLoader.implicitHeight
    height: bodyHeight + 2 * padding + 2 * shadowMargin

    /// True while a selection is captured at all; the state line's dot and label read it.
    readonly property bool capturing: Controller.settings.selectionCapture

    /// The interface language, and the name to show for it in the list.
    readonly property string language: Controller.settings.uiLanguage
    readonly property string languageName: language === "zh" ? "中文" : "English"

    /// Whether the language list is unfolded.
    property bool listVisible: false

    /// The menu rows are only needed after the tray menu is first opened.
    property bool contentActive: false

    /// The two panels the menu opens. Where a panel goes is Main.qml's business, so the menu
    /// asks for one rather than calling into the file that instantiated it: that file's `root`
    /// id does resolve at runtime, but nothing declares it and nothing checks it.
    signal statsRequested()
    signal settingsRequested()

    /// The day's tally for the statistics row, in the shape the tray tooltip uses.
    readonly property string todayFigures: Controller.stats.todayPops + " " + qsTr("words")
        + " · " + Controller.stats.currency + Controller.stats.todayCost.toFixed(2)

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

    ShadowCard {
        id: card
        x: menu.cardLeft - menu.shadowMargin
        y: 0
        width: menu.cardWidth + 2 * menu.shadowMargin
        height: menu.height
        radius: Tokens.radiusMenu

        Loader {
            id: bodyLoader
            x: menu.padding
            y: menu.padding
            width: menu.cardWidth - 2 * menu.padding
            active: menu.contentActive
            sourceComponent: Column {
                id: body
                width: menu.cardWidth - 2 * menu.padding
                spacing: 0

                // The state line: the product mark carries the state -- ok green while a
                // selection is being captured, faint when it is not -- and the text names what
                // the app is doing. The mark stands where UI.md section 4.2 used to draw an
                // anonymous dot: it says the same thing, and says whose app this is.
                //
                // Its inset and its size are the rows' beneath it (11 px, 15 px), so the mark
                // sits in the same column as the statistics, language and settings glyphs
                // instead of starting a second one.
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

                    Icon {
                        id: mark
                        anchors.left: parent.left
                        anchors.leftMargin: 11
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/lens.svg"
                        color: menu.capturing ? Tokens.ok : Tokens.faint
                    }
                    Text {
                        anchors.left: mark.right
                        anchors.leftMargin: 9
                        anchors.verticalCenter: parent.verticalCenter
                        text: menu.capturing ? Controller.modeLabel : qsTr("Selection capture is off")
                        color: Tokens.text
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                }

                Item { width: 1; height: 4 }

                MenuRow {
                    id: statsRow
                    width: parent.width
                    source: "qrc:/icons/ui-stats.svg"
                    label: qsTr("Statistics")
                    note: menu.todayFigures
                    onPicked: {
                        menu.visible = false;
                        menu.statsRequested();
                    }
                }

                MenuRow {
                    width: parent.width
                    source: "qrc:/icons/ui-language.svg"
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
                    source: "qrc:/icons/ui-settings.svg"
                    label: qsTr("Settings")
                    onPicked: {
                        menu.visible = false;
                        menu.settingsRequested();
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
                    source: "qrc:/icons/ui-quit.svg"
                    label: qsTr("Quit")
                    danger: true
                    onPicked: Qt.quit()
                }
            }
        }
    }

    Loader {
        id: languageList
        x: menu.listLeft - menu.shadowMargin
        y: menu.shadowMargin + menu.padding + menu.languageRowOffset - 6
        width: menu.listWidth + 2 * menu.shadowMargin
        height: menu.listVisible ? 2 * 28 + 10 + 2 * menu.shadowMargin : 0
        active: menu.listVisible
        visible: active
        sourceComponent: ShadowCard {
            width: menu.listWidth + 2 * menu.shadowMargin
            height: listColumn.implicitHeight + 10 + 2 * menu.shadowMargin
            radius: Tokens.radiusGroup

            Column {
                id: listColumn
                x: 5
                y: 5
                width: menu.listWidth - 10

                Repeater {
                    model: [{ code: "zh", name: "中文" }, { code: "en", name: "English" }]

                    Rectangle {
                        id: option
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
                            text: option.modelData.name
                            color: option.modelData.code === menu.language ? Tokens.on : Tokens.text
                            font.pixelSize: 13
                        }

                        HoverHandler {
                            id: rowHover
                            cursorShape: Qt.PointingHandCursor
                            onHoveredChanged: if (hovered) listHide.stop()
                        }
                        TapHandler {
                            onTapped: {
                                Controller.setUiLanguage(option.modelData.code);
                                menu.visible = false;
                            }
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
