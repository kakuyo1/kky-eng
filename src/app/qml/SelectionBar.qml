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
    readonly property int tailHeight: 7
    readonly property int gap: 8 ///< Clearance between the tail tip and the selection.

    width: content.implicitWidth + 2 * shadowMargin
    height: content.implicitHeight + 2 * shadowMargin

    /// The selection the actions apply to, set by main.qml alongside openAt().
    property string selectionText: ""

    /// @param payload The controller's {x, y, kind, text}; only x and y are used here.
    function openAt(payload) {
        // The bar hangs above the anchor. Near the top of the screen there is no room, so it
        // flips below — UI.md's open question about the flip rule, resolved as: flip, do not
        // clip.
        const anchorY = payload.y;
        const above = anchorY - height - gap;
        bar.y = above >= 0 ? above : anchorY + gap;
        bar.x = payload.x;
        visible = true;
    }

    Item {
        id: content
        x: bar.shadowMargin
        y: bar.shadowMargin
        implicitWidth: row.width + 10
        implicitHeight: row.height + 10 + bar.tailHeight

        Rectangle {
            id: tail
            width: 11
            height: 11
            x: 17
            y: row.height + 4
            color: Tokens.panel
            border.width: 1
            border.color: Tokens.line
            rotation: 45
        }

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

            Row {
                id: row
                anchors.centerIn: parent
                spacing: 2

                Repeater {
                    model: [
                        { action: "translate", label: qsTr("Translate"), icon: "M3 5.5h9 M7.5 3.5v2 M10.5 5.5c0 4-2.6 7.4-6.5 8.9 M5 9.6c1.2 2.3 3.1 4.1 5.3 5.1 M12.6 20.5l3.7-9 3.7 9 M14 17.4h4.6" },
                        { action: "explain", label: qsTr("Explain"), icon: "M12 6.6C10.4 5.2 8.3 4.5 5.5 4.5H4v13h1.5c2.8 0 4.9.7 6.5 2.1 M12 6.6c1.6-1.4 3.7-2.1 6.5-2.1H20v13h-1.5c-2.8 0-4.9.7-6.5 2.1 M12 6.6v13" },
                        { action: "copy", label: qsTr("Copy text"), icon: "M11.5 9H17.5A2.5 2.5 0 0 1 20 11.5V17.5A2.5 2.5 0 0 1 17.5 20H11.5A2.5 2.5 0 0 1 9 17.5V11.5A2.5 2.5 0 0 1 11.5 9Z M15 5.5V5A1.5 1.5 0 0 0 13.5 3.5H5A1.5 1.5 0 0 0 3.5 5V13.5A1.5 1.5 0 0 0 5 15H5.5" },
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
                                path: modelData.icon
                                color: Tokens.muted
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                color: Tokens.text
                                font.family: Tokens.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
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
