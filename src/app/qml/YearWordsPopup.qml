pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The last 365 local days of word lookups, shown as a compact contribution graph.
 *
 * The controller owns the date projection. This surface only maps it to seven rows and lets the
 * reader choose the hue used by the four non-empty levels.
 */
Window {
    id: year

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 380
    readonly property int weekCount: 53
    readonly property int weekdayWidth: 15
    readonly property int gridGap: 4
    readonly property int cellGap: 1
    readonly property int cellSize: Math.max(3, Math.floor((cardWidth - 40 - weekdayWidth - gridGap - (weekCount - 1) * cellGap) / weekCount))
    readonly property int graphHeight: cellSize * 7 + cellGap * 6
    readonly property var days: Controller.yearDays
    readonly property int dataCellCount: days.length
    readonly property int gridCellCount: weekCount * 7
    readonly property var hues: [
        {name: qsTr("Red"), hue: 0}, {name: qsTr("Orange"), hue: 30},
        {name: qsTr("Yellow"), hue: 60}, {name: qsTr("Yellow green"), hue: 90},
        {name: qsTr("Green"), hue: 120}, {name: qsTr("Teal"), hue: 150},
        {name: qsTr("Cyan"), hue: 180}, {name: qsTr("Blue"), hue: 210},
        {name: qsTr("Indigo"), hue: 240}, {name: qsTr("Violet"), hue: 270},
        {name: qsTr("Magenta"), hue: 300}, {name: qsTr("Rose"), hue: 330}
    ]

    property bool paletteOpen: false
    property int hueIndex: 4
    property int selectedLevel: 0
    property string selectedDate: ""
    readonly property var selectedDay: year.days.find(function (day) { return day.date === year.selectedDate; }) || null
    readonly property bool paletteReady: paletteLoader.status === Loader.Ready

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 34 + 2 * shadowMargin

    signal backRequested()

    function dayAt(index) {
        return index >= 0 && index < days.length ? days[index] : null;
    }

    function leadingDays() {
        const first = dayAt(0);
        if (!first)
            return 0;
        return (new Date(first.date + "T00:00:00").getDay() + 6) % 7;
    }

    function dayLevel(pops) {
        return pops === 0 ? 0 : pops === 1 ? 1 : pops <= 3 ? 2 : pops <= 5 ? 3 : 4;
    }

    function cellColor(level) {
        if (level === 0)
            return Tokens.panel2;
        const lightness = Tokens.dark ? [0.29, 0.40, 0.51, 0.63][level - 1]
                                     : [0.84, 0.64, 0.46, 0.30][level - 1];
        return Qt.hsla(hues[hueIndex].hue / 360, 0.62, lightness, 1);
    }

    function monthLabel(column) {
        const start = leadingDays();
        for (let row = 0; row < 7; ++row) {
            const day = dayAt(column * 7 + row - start);
            if (day && day.date.slice(8, 10) === "01")
                return day.date.slice(5, 7);
        }
        return "";
    }

    function openPalette(level) {
        selectedLevel = level;
        paletteOpen = true;
    }

    function closePalette() {
        paletteOpen = false;
    }

    function selectHue(index) {
        hueIndex = index;
    }

    onVisibleChanged: if (!visible)
        closePalette()

    Shortcut {
        sequence: "Escape"
        onActivated: if (year.paletteOpen) year.closePalette()
    }

    ShadowCard {
        anchors.fill: parent
        movable: true

        Column {
            id: column
            x: 20
            y: 18
            width: year.cardWidth - 40
            spacing: 0

            Item {
                width: parent.width
                height: 22

                Icon {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/icons/ui-back.svg"
                    color: Tokens.faint
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: year.backRequested() }
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Year in words")
                    color: Tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Text {
                        text: qsTr("365 days")
                        color: Tokens.faint
                        font.family: Tokens.monoFamily
                        font.pixelSize: 10
                    }
                    Icon {
                        source: "qrc:/icons/ui-close.svg"
                        color: Tokens.faint
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: year.visible = false }
                    }
                }
            }

            Item { width: 1; height: 10 }

            Item {
                width: parent.width
                height: 20

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: year.selectedDay
                          ? qsTr("%1: %2 lookups").arg(year.selectedDay.date).arg(year.selectedDay.pops)
                          : qsTr("Words explained")
                    color: Tokens.muted
                    font.pixelSize: 11
                }
                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("%1 words").arg(year.days.reduce(function (sum, day) { return sum + day.pops; }, 0))
                    color: Tokens.faint
                    font.family: Tokens.monoFamily
                    font.pixelSize: 11
                }
            }

            Item { width: 1; height: 8 }

            Item {
                id: graph
                width: parent.width
                height: monthLabels.height + gridRow.height + legend.height + 7

                Item {
                    id: monthLabels
                    width: parent.width
                    height: 12

                    Repeater {
                        model: year.weekCount
                        delegate: Text {
                            objectName: label.length > 0 ? "yearMonthLabel" : ""
                            required property int index
                            readonly property string label: year.monthLabel(index)
                            x: year.weekdayWidth + year.gridGap + index * (year.cellSize + year.cellGap)
                            width: 18
                            visible: label.length > 0
                            text: label
                            color: Tokens.faint
                            font.family: Tokens.monoFamily
                            font.pixelSize: 8
                        }
                    }
                }

                Row {
                    id: gridRow
                    y: monthLabels.height
                    spacing: year.gridGap

                    Item {
                        width: year.weekdayWidth
                        height: year.graphHeight

                        Repeater {
                            model: 7
                            delegate: Text {
                                required property int index
                                y: index * (year.cellSize + year.cellGap) - 1
                                width: year.weekdayWidth
                                height: year.cellSize + 2
                                visible: index === 1 || index === 3 || index === 5
                                text: index === 1 ? qsTr("Mon") : index === 3 ? qsTr("Wed") : qsTr("Fri")
                                color: Tokens.faint
                                font.pixelSize: 8
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Grid {
                        id: cells
                        columns: year.weekCount
                        rows: 7
                        flow: Grid.TopToBottom
                        width: year.cellSize * year.weekCount + year.cellGap * (year.weekCount - 1)
                        height: year.graphHeight
                        columnSpacing: year.cellGap
                        rowSpacing: year.cellGap

                        Repeater {
                            model: year.weekCount * 7
                            delegate: Rectangle {
                                required property int index
                                readonly property int dayIndex: index - year.leadingDays()
                                readonly property var day: year.dayAt(dayIndex)
                                width: year.cellSize
                                height: year.cellSize
                                radius: 1
                                visible: day !== null
                                color: visible ? year.cellColor(year.dayLevel(day.pops)) : "transparent"
                                border.width: visible && year.selectedDate === day.date ? 1 : 0
                                border.color: Tokens.text
                                Accessible.ignored: !visible
                                Accessible.name: visible ? qsTr("%1, %2 words").arg(day.date).arg(day.pops) : ""

                                TapHandler {
                                    enabled: parent.visible
                                    onTapped: year.selectedDate = parent.day.date
                                }
                            }
                        }
                    }
                }

                Row {
                    id: legend
                    anchors.right: parent.right
                    y: gridRow.y + gridRow.height + 7
                    spacing: 3

                    Text { text: qsTr("Less"); color: Tokens.faint; font.pixelSize: 8 }
                    Repeater {
                        model: 5
                        delegate: Rectangle {
                            id: legendCell
                            required property int index
                            width: 9
                            height: 9
                            radius: 1
                            color: year.cellColor(index)
                            border.width: year.selectedLevel === index ? 1 : 0
                            border.color: Tokens.text
                            Accessible.role: Accessible.Button
                            Accessible.name: legendCell.index === 0 ? qsTr("No words, choose color") : qsTr("Level %1, choose color").arg(legendCell.index)
                            TapHandler { onTapped: year.openPalette(legendCell.index) }
                        }
                    }
                    Text { text: qsTr("More"); color: Tokens.faint; font.pixelSize: 8 }
                }
            }

            Loader {
                id: paletteLoader
                width: parent.width
                active: year.paletteOpen
                height: active ? implicitHeight : 0
                sourceComponent: paletteComponent
            }
        }
    }

    Component {
        id: paletteComponent

        Item {
            implicitHeight: 166
            width: year.cardWidth - 40

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Tokens.line2
            }

            Item {
                id: paletteHeader
                objectName: "yearPaletteHeader"
                width: parent.width
                height: 24

                Row {
                    anchors.left: parent.left
                    anchors.right: closePalette.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Text {
                        objectName: "yearPaletteTitle"
                        text: qsTr("Cell colors")
                        color: Tokens.text
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }
                    Text {
                        objectName: "yearPaletteHueName"
                        text: qsTr("%1 hue · 4 shades").arg(year.hues[year.hueIndex].name)
                        color: Tokens.faint
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                }

                Icon {
                    id: closePalette
                    objectName: "yearPaletteClose"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/icons/ui-close.svg"
                    color: Tokens.faint
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: year.closePalette() }
                }
            }

            Row {
                id: paletteBody
                y: paletteHeader.height + 8
                width: parent.width
                spacing: 12

                Item {
                    id: wheel
                    objectName: "yearHueWheel"
                    width: 124
                    height: 124

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 12
                        radius: width / 2
                        color: "transparent"
                        border.width: 1
                        border.color: Tokens.line
                    }

                    Item {
                        id: orbit
                        anchors.fill: parent

                        RotationAnimation {
                            target: orbit
                            property: "rotation"
                            from: -20
                            to: 0
                            duration: Tokens.motion.pop
                            easing.type: Tokens.motion.easing
                            running: Tokens.motion.pop > 0
                        }

                        Repeater {
                            model: year.hues.length
                            delegate: Rectangle {
                                id: hueCell
                                required property int index
                                readonly property real angle: index * Math.PI / 6
                                readonly property int hue: year.hues[index].hue
                                x: wheel.width / 2 - width / 2 + Math.sin(angle) * 51
                                y: wheel.height / 2 - height / 2 - Math.cos(angle) * 51
                                width: 20
                                height: 20
                                radius: 10
                                color: Qt.hsla(hue / 360, 0.72, 0.52, 1)
                                border.width: year.hueIndex === index ? 2 : 1
                                border.color: year.hueIndex === index ? Tokens.text : Tokens.panel
                                Accessible.role: Accessible.Button
                                Accessible.name: year.hues[index].name
                                TapHandler { onTapped: year.selectHue(hueCell.index) }
                            }
                        }
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: 48
                        height: 48
                        radius: 24
                        color: Tokens.panel2
                        border.width: 1
                        border.color: Tokens.line
                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 8
                            text: year.hues[year.hueIndex].name
                            color: Tokens.muted
                            font.pixelSize: 8
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                    }
                }

                Column {
                    objectName: "yearShadeColumn"
                    width: paletteBody.width - wheel.width - paletteBody.spacing
                    spacing: 8

                    Text {
                        text: qsTr("Four activity levels")
                        color: Tokens.muted
                        font.pixelSize: 10
                    }

                    Row {
                        width: parent.width
                        spacing: 4

                        Repeater {
                            model: 4
                            delegate: Column {
                                id: shadeCell
                                required property int index
                                width: (parent.width - 12) / 4
                                spacing: 4

                                Rectangle {
                                    width: parent.width
                                    height: 18
                                    radius: 3
                                    color: year.cellColor(shadeCell.index + 1)
                                    border.width: year.selectedLevel === shadeCell.index + 1 ? 1 : 0
                                    border.color: Tokens.text
                                }
                                Text {
                                    width: parent.width
                                    text: shadeCell.index === 0 ? qsTr("Light") : shadeCell.index === 3 ? qsTr("Dark") : String(shadeCell.index + 1)
                                    color: Tokens.faint
                                    font.pixelSize: 8
                                    horizontalAlignment: Text.AlignHCenter
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        text: qsTr("Select a hue to replace the four active levels.")
                        color: Tokens.faint
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
