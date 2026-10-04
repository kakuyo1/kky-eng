import QtQuick
import QtQuick.Window
import QtQuick.Effects

/**
 * The explanation bubble (UI.md section 4.3), the surface the whole channel exists for.
 *
 * It stacks above the selection, the way the action bar does, and drops below it only when
 * there is no room above. The two surfaces answer the same gesture -- a finished selection --
 * so one hanging under the text and the other over it read as two unrelated things landing
 * wherever they liked. The countdown lives here
 * rather than in the controller: it is the view's behaviour, and hovering is what pauses it.
 * It never takes focus, so clicking 已会 or 新词 does not change which application the next
 * injected Ctrl+C would go to.
 */
Window {
    id: bubble

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int gap: 10
    readonly property int dismissAfterMs: 5000

    width: content.implicitWidth + 2 * shadowMargin
    height: content.implicitHeight + 2 * shadowMargin

    /// The word, entity, or sentence title and its explanation, as the controller hands them over.
    property string title: ""
    property string type: "" ///< "word", "entity", or "sentence".
    property string ipa: "" ///< Pronunciation in slashes, beside the word (app_controller.h).
    property string english: ""
    property string chinese: ""
    property string status: "" ///< "new", "known", or empty when there is no word verdict (entity / sentence).

    property bool hovering: false

    /// True while the reader is moving the card with the pointer.
    property bool dragging: false

    /// Milliseconds left before the countdown takes the bubble down. The display's number comes
    /// from here rather than from the timer's interval: hovering and dragging stop the clock, and
    /// the number has to stand still with it. `show()` refills it from `dismissAfterMs`.
    property int remainingMs: 0

    /// Where the pointer and the card were the last time the hover state changed, in screen
    /// coordinates. Nothing in the application reads them: they are what TODO.md's hover jitter
    /// needs before it can be told a real exit from something else moving `visible`, and there is
    /// no QML-to-log path in this tree that is not a probe -- so the sighting is read off these
    /// two under a debugger instead. See TODO.md "定位气泡 hover 抖动".
    property point lastHoverCursor: Qt.point(0, 0)
    property rect lastHoverCard: Qt.rect(0, 0, 0, 0)

    /// @brief Put the card above the anchor, or below it when there is no room up there.
    ///
    /// The same rule SelectionBar.openAt() follows, so the bar and the bubble answer the same
    /// gesture from the same side. Placement is by the card, not by the window: the window
    /// carries `shadowMargin` of transparent shadow on every side, so lining the window up
    /// with the word left the card a margin right of it and another one, plus the gap, below.
    function place(payload) {
        const above = payload.y - gap - height + shadowMargin;
        bubble.y = above >= 0 ? above : payload.y + gap - shadowMargin;
        bubble.x = payload.x - shadowMargin;
    }

    function show(payload) {
        dragging = false; // a fresh bubble is never mid-drag
        title = payload.title || "";
        type = payload.type || "";
        ipa = payload.ipa || "";
        english = payload.en || "";
        chinese = payload.zh || "";
        status = payload.status || "";
        remainingMs = dismissAfterMs;
        visible = true;
        place(payload);
        // ...and again once the content has laid out. `height` is the content's, and a Text
        // handed a new string has not been laid out yet when this runs: measured against the
        // real window, this read 85 where the card went on to take 132, so the card was
        // placed as though it were short and then grew down over the selection it was meant
        // to sit above. The call above is what the frame painted before layout shows, so the
        // card never jumps from somewhere else.
        Qt.callLater(place, payload);
        // Showing a surface is two steps: see Main.qml placePanel() for why `visible` on its
        // own can leave a topmost window under the taskbar.
        raise();
        updateCountdown();
    }

    onHoveringChanged: {
        Controller.bubbleHoverChanged(hovering);
        updateCountdown();
    }

    onDraggingChanged: updateCountdown()

    /// The countdown stands still while the reader is holding the bubble, under the pointer or
    /// in the middle of a drag, and picks up the time that is left when they let go of it.
    function updateCountdown() {
        if (hovering || dragging)
            countdown.stop();
        else if (visible)
            countdown.start();
    }

    Timer {
        id: countdown
        // It ticks finer than the second it displays and subtracts, rather than being the whole
        // dismissal in one shot: stopping and starting a timer begins a fresh interval, so a tick
        // as long as the display would swallow up to a second of the clock on every hover, and
        // the hover this bubble sees arrives in bursts (TODO.md, "定位气泡 hover 抖动").
        interval: 100
        repeat: true
        onTriggered: {
            bubble.remainingMs -= interval;
            if (bubble.remainingMs <= 0) {
                stop();
                bubble.visible = false;
                Controller.dismissBubble();
            }
        }
    }

    Item {
        id: content
        x: bubble.shadowMargin
        y: bubble.shadowMargin
        implicitWidth: 270
        implicitHeight: card.height

        // The shadow is cast from a shape-only copy of the card, never from the card itself:
        // MultiEffect draws its source through an offscreen texture, and at this monitor's
        // 125% scale that texture is resampled. See SelectionBar.qml for the measurement.
        Rectangle {
            id: shadowShape
            anchors.fill: card
            radius: card.radius
            color: card.color
            visible: false
        }

        MultiEffect {
            // Placed and sized by the effect, from its source. Anchoring it to the window
            // stretched the card's shape across the whole surface -- see ShadowCard.qml.
            x: card.x
            y: card.y
            source: shadowShape
            shadowEnabled: true
            shadowColor: Qt.rgba(20 / 255, 20 / 255, 26 / 255, Tokens.dark ? 0.75 : 0.26)
            shadowBlur: 0.9
            shadowVerticalOffset: 8
            blurMax: 44
        }

        Rectangle {
            id: card
            width: parent.implicitWidth
            height: column.height + 26
            radius: Tokens.radiusCard
            color: Tokens.bubbleBg
            border.width: 1
            border.color: Tokens.bubbleBorder

            // The card is the handle, through the system's own move loop. The verdict buttons
            // keep the pointer where they are, so a drag that begins on one of them is a press
            // on the button rather than a move of the window.
            // The handle a drag moves the window by; the timer below does the moving, and the
            // two reasons it is not the handler's own signal are in ShadowCard.qml. The flag
            // brackets the drag on both edges: setting it only on the way in left the countdown
            // stopped for good, and the bubble never went away again.
            DragHandler {
                id: mover
                target: null

                property point grabCursor: Qt.point(0, 0)
                property point grabWindow: Qt.point(0, 0)

                function place() {
                    const at = Controller.cursorPos();
                    bubble.x = Math.round(grabWindow.x + at.x - grabCursor.x);
                    bubble.y = Math.round(grabWindow.y + at.y - grabCursor.y);
                }

                onActiveChanged: {
                    bubble.dragging = active;
                    if (active) {
                        grabCursor = Controller.cursorPos();
                        grabWindow = Qt.point(bubble.x, bubble.y);
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

            // Leaving takes effect only if it lasts: a pointer sitting on the edge of the
            // card can read as an exit for a frame or two while the verdict row animates open,
            // and each false reading restarts the countdown and collapses the row again.
            // Entering stays immediate, so the row still opens the moment the pointer arrives.
            // ponytail: a guard, not a diagnosis -- a controlled run with the pointer parked on
            // the card showed no flapping, so the cause of the reported one is still open. If
            // it outlasts this, raise the interval.
            HoverHandler {
                onHoveredChanged: {
                    // Both readings, every flip: a real exit and something else moving `visible`
                    // look the same in the log the controller keeps, and these two are what tells
                    // them apart (TODO.md, "定位气泡 hover 抖动").
                    bubble.lastHoverCursor = Controller.cursorPos();
                    const at = card.mapToGlobal(0, 0);
                    bubble.lastHoverCard = Qt.rect(at.x, at.y, card.width, card.height);
                    if (hovered) {
                        hoverSettle.stop();
                        bubble.hovering = true;
                    } else {
                        hoverSettle.restart();
                    }
                }
            }

            Timer {
                id: hoverSettle
                interval: 140
                onTriggered: bubble.hovering = false
            }

            // The way out for a reader who does not want to wait the countdown out. It takes
            // the same two steps the countdown takes on its own, so the controller hears about
            // the dismissal the same way either way.
            Icon {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.top: parent.top
                anchors.topMargin: 15
                source: "qrc:/icons/ui-close.svg"
                color: Tokens.faint
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        countdown.stop();
                        bubble.visible = false;
                        Controller.dismissBubble();
                    }
                }
            }

            Column {
                id: column
                x: 16
                y: 14
                width: parent.width - 32
                spacing: 0

                // A word bubble carries the learning verdict; a sentence bubble carries its type
                // label. An entity bubble carries neither: a named thing is not a learning item,
                // so it is drawn as title plus encyclopaedic definition with no badge.
                Rectangle {
                    visible: bubble.type === "word" || bubble.type === "sentence"
                    width: tag.width + 16
                    height: 17
                    radius: Tokens.radiusPill
                    color: bubble.type === "word" ? (bubble.status === "known" ? Tokens.panel2 : Tokens.okBg) : "transparent"
                    border.width: bubble.type === "word" ? 0 : 1
                    border.color: Tokens.line

                    Text {
                        id: tag
                        anchors.centerIn: parent
                        text: bubble.type === "sentence" ? qsTr("Sentence") : bubble.type === "word" ? (bubble.status === "known" ? qsTr("Known") : qsTr("New")) : ""
                        color: bubble.type === "sentence" ? Tokens.muted : bubble.status === "known" ? Tokens.muted : Tokens.okText
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }
                }

                // The title and its pronunciation share one line for word bubbles: IPA is set
                // beside the word, the way a dictionary prints it. Read only --
                // it is not a control, and nothing about it invites a press. The pair is placed by
                // hand rather than by a RowLayout: a layout with no item left to grow does not
                // keep the slack at the end -- measured, a 30 px word and its pronunciation 36 px
                // apart in a 238 px row -- and a gap that wide reads as two unrelated things.
                // The prototype's 7 px is what the pair is set with.
                // A sentence bubble draws no title: the selected sentence is usually long and
                // overflows the card, and the reader still has it highlighted on screen. Word and
                // entity titles are short and stay.
                Item {
                    id: wordRow
                    visible: bubble.type !== "sentence"
                    width: parent.width
                    height: bubble.type === "sentence" ? 0 : wordText.implicitHeight

                    Text {
                        id: wordText
                        // What is left once the pronunciation has its room: a word with none left
                        // ends in an ellipsis rather than running under its own IPA.
                        width: Math.min(implicitWidth, wordRow.width - (ipaText.visible ? ipaText.implicitWidth + 7 : 0))
                        topPadding: 8
                        text: bubble.title
                        color: Tokens.text
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }

                    Text {
                        id: ipaText
                        // On the word's baseline: each item's own baseline offset is the distance
                        // from its top to that baseline, padding included.
                        x: wordText.width + 7
                        y: wordText.baselineOffset - baselineOffset
                        visible: bubble.type === "word" && bubble.ipa !== ""
                        text: bubble.ipa
                        color: Tokens.faint
                        font.pixelSize: 13 // the prototype's 12.5 px, as the definition line rounds it
                    }
                }

                Text {
                    visible: bubble.english !== ""
                    width: parent.width
                    topPadding: 6
                    text: bubble.english
                    color: Tokens.muted
                    font.pixelSize: 13
                    lineHeight: 1.55
                    wrapMode: Text.Wrap
                }

                Text {
                    visible: bubble.chinese !== ""
                    width: parent.width
                    topPadding: 5
                    text: bubble.chinese
                    color: Tokens.text
                    font.pixelSize: 12
                    lineHeight: 1.5
                    wrapMode: Text.Wrap
                }

                // Hover expands the verdict buttons; the row contributes no height when shut.
                // Only a real explanation has a verdict to give: a notice's title is not a
                // word, so a press there would write a sentence into the word store.
                Item {
                    readonly property bool show: bubble.hovering && bubble.status !== ""
                    width: parent.width
                    height: show ? actions.height + 25 : 0
                    clip: true
                    opacity: show ? 1 : 0
                    Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.InOutQuad } }
                    Behavior on opacity { NumberAnimation { duration: 220 } }

                    Rectangle {
                        anchors.top: parent.top
                        anchors.topMargin: 12
                        width: parent.width
                        height: 1
                        color: Tokens.line2
                    }

                    Row {
                        id: actions
                        anchors.top: parent.top
                        anchors.topMargin: 25
                        spacing: 8

                        Rectangle {
                            width: knownLabel.width + 26
                            height: knownLabel.height + 12
                            radius: Tokens.radiusPill
                            color: Tokens.panel2
                            border.width: 1
                            border.color: Tokens.line
                            scale: knownTap.pressed ? 0.97 : 1.0
                            Text {
                                id: knownLabel
                                anchors.centerIn: parent
                                text: qsTr("Known")
                                color: Tokens.text
                                font.pixelSize: 12
                                font.weight: Font.Bold
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                id: knownTap
                                onTapped: Controller.mark(bubble.title, true)
                            }
                        }

                        Rectangle {
                            width: newLabel.width + 26
                            height: newLabel.height + 12
                            radius: Tokens.radiusPill
                            color: Tokens.ink
                            scale: newTap.pressed ? 0.97 : 1.0
                            Text {
                                id: newLabel
                                anchors.centerIn: parent
                                text: qsTr("New")
                                color: Tokens.on
                                font.pixelSize: 12
                                font.weight: Font.Bold
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                id: newTap
                                onTapped: Controller.mark(bubble.title, false)
                            }
                        }
                    }
                }

                Text {
                    width: parent.width
                    topPadding: 9
                    // The remaining seconds, not the setting that seeded them: the count stands
                    // still while the pointer holds the bubble and steps down again when it goes.
                    text: qsTr("Disappears in %1s").arg(Math.ceil(bubble.remainingMs / 1000))
                    color: Tokens.faint
                    font.pixelSize: 11
                }

            }
        }
    }
}
