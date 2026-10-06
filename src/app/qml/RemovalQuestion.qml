pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The question the words panel's cross asks, as a window of its own over that panel.
 *
 * A window rather than a face drawn inside the panel's card, for two reasons. The card's height is
 * not the panel's to change -- Main.placeBeside puts its bottom edge on the tray icon and nothing
 * re-places a panel that changes size afterwards, so a card that shrank for the question would
 * lift away from the icon it was opened from. And it is a window: docs/QML.md section 1 draws the
 * line between a surface and a component at exactly that, so it is a file of its own.
 *
 * It is modal to the window that raises it, so a press that lands on the list behind it cannot
 * reach a row: deleting one word while being asked about another is not a thing to allow.
 *
 * Neither answer pill is filled. The sentence is what carries the decision -- PRODUCT.md asks a
 * removal to confirm before it happens, not to alarm the reader into it.
 *
 * Its strings are spelled in the words panel's translation context rather than this file's, the
 * way SettingsPopup's sub-pages are: the question belongs to that panel, and a context of its own
 * would read as a new string to every translator.
 */
Window {
    id: root

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    modality: Qt.WindowModal

    /// The lemma being asked about. The caller owns it -- this window only draws it.
    property string word: ""

    /// The answer. @p dontAsk is what the switch under the sentence says.
    signal answered(bool dontAsk)

    readonly property int shadowMargin: 26
    readonly property int minCardWidth: 320
    readonly property int maxCardWidth: 460

    /// Whether this answer also settles the asking. On when the run's first question is put, so
    /// one removal a run is asked about; the reader turns it off to be asked about the ones after
    /// that, and it stays where the reader left it.
    ///
    /// Not reset per question on purpose: `components/Switch.qml` writes its `checked` itself when
    /// it is tapped, so writing this from elsewhere leaves the switch showing one thing and the
    /// decision reading another.
    property bool dontAsk: true

    /// As wide as the sentence needs and no wider, the way the panel is as wide as its widest row:
    /// 320 px is the floor, and past 460 a long word or a long translation wraps rather than
    /// widening the card off the screen.
    readonly property int cardWidth: Math.min(maxCardWidth,
                                              Math.max(minCardWidth, Math.ceil(questionText.implicitWidth) + 40))
    width: cardWidth + 2 * shadowMargin
    height: questionColumn.implicitHeight + 36 + 2 * shadowMargin

    /**
     * @brief Put the question up, centred on the window it is modal to.
     *
     * The centring waits a turn: the height is the laid-out height of what the card holds, and
     * that is not settled on the same turn the window is shown.
     */
    function ask() {
        root.visible = true;
        root.raise();
        Qt.callLater(() => root.placeOverCard());
    }

    /// @brief Centre the question on the window it is modal to.
    function placeOverCard() {
        const host = root.transientParent;
        if (!host)
            return;
        root.x = Math.round(host.x + (host.width - root.width) / 2);
        root.y = Math.round(host.y + (host.height - root.height) / 2);
    }

    /// Escape means "not this word", and is offered exactly while the question is up.
    ///
    /// Application-wide rather than the settings panel's default window scope, because the
    /// offscreen platform the suite runs on never gives a window the active state and a
    /// window-scoped Escape cannot be exercised there. The wider scope is not a wider net: Qt
    /// consults a shortcut map only for keys some window of this application received, so
    /// `enabled` is the whole guard.
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: root.visible
        onActivated: root.visible = false
    }

    ShadowCard {
        anchors.fill: parent

        Column {
            id: questionColumn
            x: 20
            y: 18
            width: root.cardWidth - 40
            spacing: 12

            Text {
                width: parent.width
                text: qsTranslate("WordsPopup", "Remove word")
                color: Tokens.text
                font.pixelSize: 14
                font.weight: Font.Bold
            }

            // The word being removed is the sentence's own %1 rather than a field of its own:
            // where it sits in that sentence is the translator's to place.
            Text {
                id: questionText
                width: parent.width
                text: qsTranslate("WordsPopup", "Remove %1 from your word list and history?").arg(root.word)
                color: Tokens.text
                font.pixelSize: 13
                lineHeight: 1.35
                lineHeightMode: Text.ProportionalHeight
                wrapMode: Text.WordWrap
            }

            // The only control here that is not an answer to the removal: it decides whether this
            // question is asked again, and it is read when the answer is Remove.
            SwitchRow {
                label: qsTranslate("WordsPopup", "Don't ask again")
                checked: root.dontAsk
                onToggled: (on) => root.dontAsk = on
            }

            Item {
                width: parent.width
                height: buttons.height

                Row {
                    id: buttons
                    anchors.right: parent.right
                    spacing: 8

                    Rectangle {
                        width: cancelLabel.width + 28
                        height: 27
                        radius: Tokens.radiusPill
                        color: Tokens.panel2
                        border.width: 1
                        border.color: Tokens.line
                        scale: cancelTap.pressed ? 0.97 : 1.0
                        Behavior on scale {
                            NumberAnimation {
                                duration: Tokens.motion.press
                                easing.type: Tokens.motion.easing
                            }
                        }

                        Text {
                            id: cancelLabel
                            anchors.centerIn: parent
                            text: qsTranslate("WordsPopup", "Cancel")
                            color: Tokens.muted
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            id: cancelTap
                            onTapped: root.visible = false
                        }
                    }

                    Rectangle {
                        width: removeLabel.width + 28
                        height: 27
                        radius: Tokens.radiusPill
                        color: Tokens.panel2
                        border.width: 1
                        border.color: Tokens.line
                        scale: removeTap.pressed ? 0.97 : 1.0
                        Behavior on scale {
                            NumberAnimation {
                                duration: Tokens.motion.press
                                easing.type: Tokens.motion.easing
                            }
                        }

                        Text {
                            id: removeLabel
                            anchors.centerIn: parent
                            text: qsTranslate("WordsPopup", "Remove")
                            color: Tokens.text
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            id: removeTap
                            onTapped: {
                                root.answered(root.dontAsk);
                                root.visible = false;
                            }
                        }
                    }
                }
            }
        }
    }
}
