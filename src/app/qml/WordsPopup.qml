pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import QtQuick.Window

/**
 * Every word the bubble has shown (UI.md section 4.7), newest first, with a filter across the
 * top. The controller hands over finished strings: the relative time labels depend on the
 * current date, which is not something a view should be working out.
 *
 * It is also where a word is settled: each row carries the bubble's own two verdict pills, so a
 * word the reader did not mark while the bubble was up (or the countdown took away) can be
 * marked here instead.
 */
Window {
    id: words

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 320

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 34 + 2 * shadowMargin

    /// 0 = all, 1 = known, 2 = new.
    property int filter: 0
    property string pendingRemoval: ""

    /// The filter above, spelled the way exportWords() and saveWords() take it. The list and
    /// an export are the same rows seen twice, so an export follows whatever is on screen.
    readonly property var scopes: ["all", "known", "new"]
    readonly property string scope: scopes[filter]

    /// The reader asked for the panel this one was opened from.
    signal backRequested()

    ShadowCard {
        anchors.fill: parent

        Column {
            id: column
            x: 20
            y: 18
            width: words.cardWidth - 40
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
                    text: qsTr("Words")
                    color: Tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }

                // The title row is the handle rather than the card, because the card holds the
                // list: a drag inside a list is a scroll, and a handler covering it would be
                // competing with the flick for the same gesture. The timer below does the
                // moving, and the two reasons it is not the handler's own signal are in
                // ShadowCard.qml.
                DragHandler {
                    id: mover
                    target: null

                    property point grabCursor: Qt.point(0, 0)
                    property point grabWindow: Qt.point(0, 0)

                    function place() {
                        const at = Controller.cursorPos();
                        words.x = Math.round(grabWindow.x + at.x - grabCursor.x);
                        words.y = Math.round(grabWindow.y + at.y - grabCursor.y);
                    }

                    onActiveChanged: {
                        if (active) {
                            grabCursor = Controller.cursorPos();
                            grabWindow = Qt.point(words.x, words.y);
                        } else {
                            place();
                        }
                    }
                }

                Timer {
                    interval: 16
                    repeat: true
                    running: mover.active
                    onTriggered: mover.place()
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
                        TapHandler { onTapped: words.backRequested() }
                    }
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/ui-close.svg"
                        color: Tokens.faint
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: words.visible = false }
                    }
                }
            }

            // The list's own line: how many words, and the way out to a file. The export sits
            // here rather than in the title row's icon pair because it acts on the list -- and
            // on the filter below it -- while those two glyphs are about the window.
            Item {
                width: parent.width
                height: exportPill.height

                Text {
                    id: count
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    // The list's own count, not the all-time tally the statistics panel shows:
                    // the rows below are deduplicated, so a word explained twice counts once here
                    // and twice there. A number that disagrees with the list under it reads as a
                    // bug, whichever of the two meanings it was meant to carry.
                    text: qsTr("%1 words").arg(Controller.words.length)
                    color: Tokens.faint
                    font.pixelSize: 12
                }

                Rectangle {
                    id: exportPill
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: exportLabel.width + 18
                    height: 20
                    radius: Tokens.radiusPill
                    color: Tokens.panel2
                    border.width: 1
                    border.color: Tokens.line
                    scale: exportTap.pressed ? 0.97 : 1.0
                    Behavior on scale {
                        NumberAnimation {
                            duration: Tokens.motion.press
                            easing.type: Tokens.motion.easing
                        }
                    }

                    Text {
                        id: exportLabel
                        anchors.centerIn: parent
                        text: qsTr("Export")
                        color: Tokens.muted
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }

                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        id: exportTap
                        onTapped: saver.open()
                    }
                }
            }

            Item { width: 1; height: 12 }
            Segment {
                width: parent.width
                height: 28
                labels: [qsTr("All"), qsTr("Known"), qsTr("New")]
                currentIndex: words.filter
                onPicked: (index) => words.filter = index
            }

            Item { width: 1; height: 13 }
            Rectangle { width: parent.width; height: 1; color: Tokens.line2 }
            Item { width: 1; height: 6 }

            Item {
                width: parent.width
                // UI.md 4.7 gives the list a *maximum* of 306px: a handful of words leaves a
                // panel sized to those words, not a panel with a hole under them.
                height: Math.min(306, list.contentHeight)

                ListView {
                    id: list
                    width: parent.width
                    height: parent.height
                    clip: true
                    model: Controller.words
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Item {
                        id: entry
                        required property var modelData

                        width: words.cardWidth - 40

                        /// Whether the filter above shows this word. It is the row's presence
                        /// rather than a switch, which is what the height below animates.
                        ///
                        /// The verdict is compared by key, not by the translated label: the
                        /// two would drift apart the moment a translation changed.
                        readonly property bool inScope: words.filter === 0
                                                        || (words.filter === 1 && modelData.verdict === "known")
                                                        || (words.filter === 2 && modelData.verdict === "new")

                        // A row arrives and leaves by its height, because that is what the
                        // list's layout reads: a model change rebuilds every delegate, so the
                        // ListView's own add/remove transitions never run here, and the movement
                        // the reader actually sees is a filtered row folding shut. `visible`
                        // follows the animation rather than the filter, so a row on its way out
                        // is still drawn while it collapses; `clip` is what keeps its contents
                        // from spilling over the rows it is collapsing between.
                        height: inScope ? 31 : 0
                        visible: inScope || height > 0
                        opacity: inScope ? 1 : 0
                        clip: true
                        Behavior on height {
                            NumberAnimation {
                                duration: Tokens.motion.pop
                                easing.type: Tokens.motion.easing
                            }
                        }
                        Behavior on opacity {
                            NumberAnimation {
                                duration: Tokens.motion.pop
                                easing.type: Tokens.motion.easing
                            }
                        }

                        Text {
                            anchors.left: parent.left
                            // The word takes what the right-hand side leaves, so a long one
                            // elides rather than pushing the verdict pills off the card.
                            anchors.right: side.left
                            anchors.rightMargin: 9
                            anchors.verticalCenter: parent.verticalCenter
                            text: entry.modelData.word
                            color: Tokens.text
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }

                        Row {
                            id: side
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8

                            // When the word was last shown, and how many times it has been.
                            // Both are the same faint metadata, so they sit in one cluster
                            // rather than as two more columns.
                            Row {
                                id: meta
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6

                                MixedText {
                                    anchors.verticalCenter: parent.verticalCenter
                                    value: entry.modelData.when
                                    color: Tokens.faint
                                    pixelSize: 11
                                }

                                MixedText {
                                    anchors.verticalCenter: parent.verticalCenter
                                    // The history is what the count is read from, and it is
                                    // capped, so this is a floor once a word outlives it.
                                    value: qsTr("%1×").arg(entry.modelData.pops)
                                    color: Tokens.faint
                                    pixelSize: 11
                                }
                            }

                            // The bubble's two verdict pills, in the same two shapes, with
                            // this word's verdict filled instead of both being actions. A word
                            // that was never marked leaves both plain, which is what it is.
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6

                                Rectangle {
                                    id: knownPill
                                    readonly property bool current: entry.modelData.verdict === "known"
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: knownLabel.width + 13
                                    height: 20
                                    radius: Tokens.radiusPill
                                    color: current ? Tokens.ink : Tokens.panel2
                                    border.width: 1
                                    border.color: current ? Tokens.ink : Tokens.line
                                    scale: knownTap.pressed ? 0.97 : 1.0
                                    Behavior on scale {
                                        NumberAnimation {
                                            duration: Tokens.motion.press
                                            easing.type: Tokens.motion.easing
                                        }
                                    }

                                    Text {
                                        id: knownLabel
                                        anchors.centerIn: parent
                                        text: qsTr("Known")
                                        color: knownPill.current ? Tokens.on : Tokens.muted
                                        font.pixelSize: 11
                                        font.weight: Font.Bold
                                    }

                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    TapHandler {
                                        id: knownTap
                                        onTapped: Controller.mark(entry.modelData.word, true)
                                    }
                                }

                                Rectangle {
                                    id: newPill
                                    readonly property bool current: entry.modelData.verdict === "new"
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: newLabel.width + 13
                                    height: 20
                                    radius: Tokens.radiusPill
                                    color: current ? Tokens.ink : Tokens.panel2
                                    border.width: 1
                                    border.color: current ? Tokens.ink : Tokens.line
                                    scale: newTap.pressed ? 0.97 : 1.0
                                    Behavior on scale {
                                        NumberAnimation {
                                            duration: Tokens.motion.press
                                            easing.type: Tokens.motion.easing
                                        }
                                    }

                                    Text {
                                        id: newLabel
                                        anchors.centerIn: parent
                                        text: qsTr("New")
                                        color: newPill.current ? Tokens.on : Tokens.muted
                                        font.pixelSize: 11
                                        font.weight: Font.Bold
                                    }

                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    TapHandler {
                                        id: newTap
                                        onTapped: Controller.mark(entry.modelData.word, false)
                                    }
                                }
                            }

                            Icon {
                                width: 14
                                height: 14
                                source: "qrc:/icons/ui-close.svg"
                                color: Tokens.faint
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                TapHandler {
                                    onTapped: {
                                        words.pendingRemoval = entry.modelData.word
                                        remover.open()
                                    }
                                }
                            }
                        }
                    }
                }

                // The six-pixel scrollbar UI.md 4.7 asks for, drawn rather than imported so
                // the module needs no widget styling of its own. It sits in the card's right
                // padding, clear of the rows: a bar against the list's edge draws its rounded
                // end over the verdict pill on every row it passes.
                Rectangle {
                    visible: list.contentHeight > list.height
                    width: 6
                    radius: 3
                    color: Tokens.line
                    x: parent.width + 6
                    height: Math.max(24, list.height * list.height / list.contentHeight)
                    y: list.contentHeight > list.height
                       ? list.contentY / (list.contentHeight - list.height) * (list.height - height)
                       : 0
                }
            }
        }
    }

    // Declared in this file rather than in Main.qml: the file holds the words list, so the
    // surface that shows the list is the one that chooses the destination. QtQuick.Dialogs can
    // name a file but cannot write one -- that half is Controller.saveWords(), which takes this
    // dialog's answer. A write that fails is logged there; this surface has nothing to report it
    // on yet.
    FileDialog {
        id: saver
        title: qsTr("Export words")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: [qsTr("Text files (*.txt)"), qsTr("All files (*)")]
        // The scope is in the name because the scope is what the export is: the reader who
        // exports twice from two filters gets two files rather than one overwriting the other.
        currentFile: "words-" + words.scope + ".txt"
        onAccepted: Controller.saveWords(saver.selectedFile, words.scope)
    }

    MessageDialog {
        id: remover
        title: qsTr("Remove word")
        text: qsTr("Remove %1 from your word list and history?").arg(words.pendingRemoval)
        buttons: MessageDialog.Yes | MessageDialog.No
        onAccepted: {
            Controller.removeWord(words.pendingRemoval)
            words.pendingRemoval = ""
        }
    }
}
