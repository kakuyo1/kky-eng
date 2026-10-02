import QtQuick
import QtQuick.Window
import QtQuick.Effects

/**
 * The bar that comes up when the reader finishes selecting text (UI.md section 4.9).
 *
 * It sits above the selection with its tail pointing down at the anchor. It never takes
 * focus, so clicking one of its items leaves the reader's application in front and the next
 * injected Ctrl+C still lands in the right place.
 */
Window {
    id: bar

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int gap: 8 ///< Clearance between the bar and the selection.

    width: content.implicitWidth + 2 * shadowMargin
    height: content.implicitHeight + 2 * shadowMargin

    /// The selection the actions apply to, set by main.qml alongside openAt().
    property string selectionText: ""

    /// @param payload The controller's {x, y, kind, text}; only x and y are used here.
    function openAt(payload) {
        // The bar hangs above the anchor. Near the top of the screen there is no room, so it
        // flips below — UI.md's open question about the flip rule, resolved as: flip, do not
        // clip.
        //
        // Every placement here is by the card, not by the window: the window is `shadowMargin`
        // bigger than the card on every side, so lining the window up with the anchor left the
        // card a shadow-margin right of the selection and another one, plus the gap, above it --
        // which read as the bar not belonging to the text at all.
        const anchorY = payload.y;
        const above = anchorY - gap - height + shadowMargin;
        bar.y = above >= 0 ? above : anchorY + gap - shadowMargin;
        bar.x = payload.x - shadowMargin;
        visible = true;
    }

    Item {
        id: content
        x: bar.shadowMargin
        y: bar.shadowMargin
        implicitWidth: row.width + 10
        implicitHeight: row.height + 10

        // The shadow is cast from a shape-only copy of the card, never from the card itself.
        // MultiEffect draws its source through an offscreen texture, and at this monitor's
        // 125% scale that texture is resampled: measured on the real window, one glyph stem
        // comes out twice as wide and half as dark as the same text drawn directly. The shape
        // can afford it; the text cannot. So the shape goes through the effect, the text does
        // not, and both the shape and the effect are declared before the card to paint under it.
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
            shadowColor: Qt.rgba(20 / 255, 20 / 255, 26 / 255, Tokens.dark ? 0.75 : 0.24)
            shadowBlur: 0.9
            shadowVerticalOffset: 10
            blurMax: 40
        }

        Rectangle {
            id: card
            width: parent.implicitWidth
            height: row.height + 10
            radius: Tokens.radiusMenu
            color: Tokens.panel
            border.width: 1
            border.color: Tokens.line

            // The card is the handle. The three items keep the pointer over themselves, so
            // pressing one is a press on the item. The timer below does the moving, and the two
            // reasons it is not the handler's own signal are in ShadowCard.qml.
            DragHandler {
                id: mover
                target: null

                property point grabCursor: Qt.point(0, 0)
                property point grabWindow: Qt.point(0, 0)

                function place() {
                    const at = controller.cursorPos();
                    bar.x = Math.round(grabWindow.x + at.x - grabCursor.x);
                    bar.y = Math.round(grabWindow.y + at.y - grabCursor.y);
                }

                onActiveChanged: {
                    if (active) {
                        grabCursor = controller.cursorPos();
                        grabWindow = Qt.point(bar.x, bar.y);
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
                id: row
                anchors.centerIn: parent
                spacing: 2

                Repeater {
                    model: [
                        { action: "translate", label: qsTr("Translate"), source: "qrc:/icons/ui-translate.svg" },
                        { action: "explain", label: qsTr("Explain"), source: "qrc:/icons/ui-explain.svg" },
                        { action: "copy", label: qsTr("Copy text"), source: "qrc:/icons/ui-copy.svg" },
                    ]

                    delegate: Rectangle {
                        id: item
                        width: itemRow.width + 22
                        height: itemRow.height + 14
                        radius: Tokens.radiusField
                        color: hover.hovered ? Tokens.panel2 : "transparent"
                        scale: hover.pressed ? 0.97 : 1.0
                        Behavior on scale { NumberAnimation { duration: 150 } }

                        Row {
                            id: itemRow
                            anchors.centerIn: parent
                            spacing: 7
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                source: modelData.source
                                color: Tokens.muted
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                color: Tokens.text
                                font.pixelSize: 13
                                font.weight: Font.Bold
                            }
                        }

                        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                controller.runSelectionAction(modelData.action, bar.selectionText);
                                bar.visible = false;
                            }
                        }
                    }
                }
            }
        }
    }
}
