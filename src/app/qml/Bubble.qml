import QtQuick
import QtQuick.Window
import QtQuick.Effects

/**
 * The explanation bubble (UI.md section 4.3), the surface the whole channel exists for.
 *
 * It hangs below the anchor with its tail pointing up at the word. The countdown lives here
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
    readonly property int tailHeight: 7
    readonly property int gap: 10
    readonly property int dismissAfterMs: 5000

    width: content.implicitWidth + 2 * shadowMargin
    height: content.implicitHeight + 2 * shadowMargin

    /// The word and its explanation, as the controller hands them over.
    property string word: ""
    property string english: ""
    property string chinese: ""
    property string status: "" ///< "new", "known", or empty for a plain notice.

    property bool hovering: false

    function show(payload) {
        word = payload.word;
        english = payload.en;
        chinese = payload.zh;
        status = payload.status;
        // Below the anchor, and lifted above it when there is no room underneath.
        const below = payload.y + gap;
        bubble.y = below + height <= screen.height ? below : payload.y - height - gap;
        bubble.x = payload.x - 34; // the tail sits 34px in from the card's left edge
        visible = true;
        countdown.restart();
    }

    onHoveringChanged: {
        controller.bubbleHoverChanged(hovering);
        if (hovering)
            countdown.stop();
        else if (visible)
            countdown.restart();
    }

    Timer {
        id: countdown
        interval: bubble.dismissAfterMs
        onTriggered: {
            bubble.visible = false;
            controller.dismissBubble();
        }
    }

    Item {
        id: content
        x: bubble.shadowMargin
        y: bubble.shadowMargin
        implicitWidth: 270
        implicitHeight: card.height + bubble.tailHeight

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
            id: tail
            width: 14
            height: 14
            x: 28
            y: -7
            radius: 3
            color: Tokens.bubbleBg
            border.width: 1
            border.color: Tokens.bubbleBorder
            rotation: 45
        }

        Rectangle {
            id: card
            width: parent.implicitWidth
            height: column.height + 26
            radius: Tokens.radiusCard
            color: Tokens.bubbleBg
            border.width: 1
            border.color: Tokens.bubbleBorder

            // Leaving takes effect only if it lasts: a pointer sitting on the edge of the
            // card can read as an exit for a frame or two while the verdict row animates open,
            // and each false reading restarts the countdown and collapses the row again.
            // Entering stays immediate, so the row still opens the moment the pointer arrives.
            // ponytail: a guard, not a diagnosis -- a controlled run with the pointer parked on
            // the card showed no flapping, so the cause of the reported one is still open. If
            // it outlasts this, raise the interval.
            HoverHandler {
                onHoveredChanged: {
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

            Column {
                id: column
                x: 16
                y: 14
                width: parent.width - 32
                spacing: 0

                // The tag row is absent when there is no status: a failed request has no
                // verdict to report, and inventing one would be a lie about what happened.
                Rectangle {
                    visible: bubble.status !== ""
                    width: tag.width + 16
                    height: 17
                    radius: Tokens.radiusPill
                    color: bubble.status === "known" ? Tokens.panel2 : Tokens.okBg

                    Text {
                        id: tag
                        anchors.centerIn: parent
                        text: bubble.status === "known" ? qsTr("Known") : qsTr("New")
                        color: bubble.status === "known" ? Tokens.muted : Tokens.okText
                        font.family: Tokens.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }

                Text {
                    width: parent.width
                    topPadding: 8
                    text: bubble.word
                    color: Tokens.text
                    font.family: Tokens.fontFamily
                    font.pixelSize: 20
                    font.weight: 650
                    elide: Text.ElideRight
                }

                Text {
                    visible: bubble.english !== ""
                    width: parent.width
                    topPadding: 6
                    text: bubble.english
                    color: Tokens.muted
                    font.family: Tokens.fontFamily
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
                    font.family: Tokens.fontFamily
                    font.pixelSize: 12
                    lineHeight: 1.5
                    wrapMode: Text.Wrap
                }

                // Hover expands the verdict buttons; the row contributes no height when shut.
                Item {
                    width: parent.width
                    height: bubble.hovering ? actions.height + 25 : 0
                    clip: true
                    opacity: bubble.hovering ? 1 : 0
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
                                font.family: Tokens.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                id: knownTap
                                onTapped: controller.mark(bubble.word, true)
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
                                font.family: Tokens.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                id: newTap
                                onTapped: controller.mark(bubble.word, false)
                            }
                        }
                    }
                }

                Text {
                    width: parent.width
                    topPadding: 9
                    text: qsTr("Disappears in %1s").arg(bubble.dismissAfterMs / 1000)
                    color: Tokens.faint
                    font.family: Tokens.fontFamily
                    font.pixelSize: 11
                }
            }
        }
    }
}
