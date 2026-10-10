import QtQuick
import QtQuick.Window

/**
 * The card a new version raises.
 *
 * A passive surface: it takes no part in the panels' mutual exclusion, and it never uses the
 * explanation notice slot, so a card standing can neither replace a notice nor be replaced by
 * one. Its close path clears only the card's own flag -- the version the reader skipped is a
 * separate decision the card's Skip button makes.
 *
 * The card also carries the download: what state it is in, how far it has got, and the one
 * thing a reader can do about it. View opens the release page in the browser; Download fetches
 * the installer and Install runs it, and only after the file has been checked against the
 * digest the same release publishes. The texts here are the duty's states drawn as words --
 * nothing a transfer said reaches this surface.
 */
Window {
    id: card

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 340

    /// The version on offer, the release page to open, and the notes to read.
    property string version: ""
    property string pageUrl: ""
    property string notes: ""

    /// The download's own state, read straight off the controller: the card is the surface
    /// that shows it, and the duty is the one that owns it.
    readonly property var download: Controller.download

    /// View was pressed: open the release page in the browser.
    signal viewRequested()
    /// Skip was pressed: stop asking about this exact version.
    signal skipRequested()
    /// Download was pressed: fetch the installer for this version.
    signal downloadRequested()
    /// Cancel was pressed: stop the transfer and delete what it wrote.
    signal cancelRequested()
    /// Install was pressed: run the installer that has been checked.
    signal installRequested()

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 36 + 2 * shadowMargin

    /// What the line under the title says, for one download state.
    ///
    /// A function of the state, so the surface can never carry a transport's own words: the
    /// failure text is picked from three fixed sentences by key. `idle` with nothing to show
    /// says nothing at all.
    function downloadText(state, percent, failure) {
        if (state === "downloading")
            return percent > 0
                    ? qsTranslate("UpdateCard", "Downloading... %1%").arg(percent)
                    : qsTranslate("UpdateCard", "Downloading...");
        if (state === "verifying")
            return qsTranslate("UpdateCard", "Checking the download...");
        if (state === "ready")
            return qsTranslate("UpdateCard", "The installer is ready to run.");
        if (state !== "failed")
            return "";
        if (failure === "proxy")
            return qsTranslate("UpdateCard", "Downloads from GitHub usually need a proxy in mainland China. Turn on your proxy and try again.");
        if (failure === "checksum")
            return qsTranslate("UpdateCard", "The download did not match the published checksum. It was deleted.");
        return qsTranslate("UpdateCard", "Download failed. Check your connection.");
    }

    /// What the primary button says in this state, and whether it is there at all.
    function primaryAction(state, available) {
        if (state === "downloading")
            return qsTranslate("UpdateCard", "Cancel");
        if (state === "ready")
            return qsTranslate("UpdateCard", "Install");
        if (state === "failed" || state === "idle")
            return available ? qsTranslate("UpdateCard", "Download") : "";
        return "";
    }

    /// Put the card in the middle of the current screen and raise it, the way Notice.qml does.
    function show(payload) {
        version = payload.version || ""
        pageUrl = payload.pageUrl || ""
        notes = payload.notes || ""
        x = Math.round(Screen.virtualX + (Screen.width - width) / 2)
        y = Math.round(Screen.virtualY + (Screen.height - height) / 2)
        visible = true
        raise()
    }

    /// Take the card down. It writes nothing: the cross says "not now", and Skip says "not this
    /// version", which are different answers to the same question.
    function closeCard() {
        visible = false
        Controller.closeUpdateCard()
    }

    ShadowCard {
        anchors.fill: parent

        Column {
            id: column
            x: 20
            y: 18
            width: card.cardWidth - 40
            spacing: 12

            Item {
                width: parent.width
                height: title.implicitHeight

                Text {
                    id: title
                    anchors.left: parent.left
                    anchors.right: close.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTranslate("UpdateCard", "Lens %1 is available").arg(card.version)
                    color: Tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }

                Icon {
                    id: close
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/icons/ui-close.svg"
                    color: Tokens.faint
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: card.closeCard() }
                }
            }

            // Empty rather than zero-height when there is nothing to read: a positioner skips an
            // invisible child whole, and an empty Text still opens a gap.
            Text {
                width: parent.width
                text: card.notes
                visible: text !== ""
                color: Tokens.faint
                font.pixelSize: 13
                lineHeight: 1.35
                lineHeightMode: Text.ProportionalHeight
                wrapMode: Text.WordWrap
            }

            // What the download is doing, or why it stopped. Empty rather than zero-height when
            // there is nothing to say, for the reason the notes line above does it.
            Text {
                objectName: "downloadLine"
                width: parent.width
                text: card.downloadText(card.download.state, card.download.percent, card.download.failure)
                visible: text !== ""
                color: card.download.state === "failed" ? Tokens.danger : Tokens.muted
                font.pixelSize: 12
                lineHeight: 1.35
                lineHeightMode: Text.ProportionalHeight
                wrapMode: Text.WordWrap
            }

            Item {
                width: parent.width
                height: row.height

                Row {
                    id: row
                    width: (primaryLabel.text !== "" ? primary.width + 12 : 0) + viewButton.width + 12 + skipButton.width
                    height: 26
                    spacing: 12

                    // The one button that changes what it means: Download while the file can be
                    // fetched, Cancel while it is, Install once it has been checked. Nothing at
                    // all when this release is not downloadable: an empty pill is a button that
                    // does nothing.
                    Rectangle {
                        id: primary
                        visible: primaryLabel.text !== ""
                        width: primaryLabel.implicitWidth + 26
                        height: 26
                        radius: Tokens.radiusPill
                        color: Tokens.ink
                        opacity: card.download.state === "downloading" ? 0.58 : 1
                        scale: primaryTap.pressed ? 0.97 : 1.0
                        Behavior on scale {
                            NumberAnimation {
                                duration: Tokens.motion.press
                                easing.type: Tokens.motion.easing
                            }
                        }

                        Text {
                            id: primaryLabel
                            anchors.centerIn: parent
                            text: card.primaryAction(card.download.state, card.download.available)
                            color: Tokens.on
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            id: primaryTap
                            onTapped: {
                                if (card.download.state === "downloading")
                                    card.cancelRequested();
                                else if (card.download.state === "ready")
                                    card.installRequested();
                                else
                                    card.downloadRequested();
                            }
                        }
                    }

                    Rectangle {
                        id: viewButton
                        width: viewLabel.width + 26
                        height: 26
                        radius: Tokens.radiusPill
                        color: Tokens.ink
                        scale: viewTap.pressed ? 0.97 : 1.0
                        Behavior on scale {
                            NumberAnimation {
                                duration: Tokens.motion.press
                                easing.type: Tokens.motion.easing
                            }
                        }

                        Text {
                            id: viewLabel
                            anchors.centerIn: parent
                            text: qsTranslate("UpdateCard", "View")
                            color: Tokens.on
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            id: viewTap
                            onTapped: {
                                card.visible = false
                                card.viewRequested()
                            }
                        }
                    }

                    Rectangle {
                        id: skipButton
                        width: skipLabel.width + 26
                        height: 26
                        radius: Tokens.radiusPill
                        color: "transparent"
                        border.width: 1
                        border.color: Tokens.line
                        scale: skipTap.pressed ? 0.97 : 1.0
                        Behavior on scale {
                            NumberAnimation {
                                duration: Tokens.motion.press
                                easing.type: Tokens.motion.easing
                            }
                        }

                        Text {
                            id: skipLabel
                            anchors.centerIn: parent
                            text: qsTranslate("UpdateCard", "Skip this version")
                            color: Tokens.text
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            id: skipTap
                            onTapped: {
                                card.visible = false
                                card.skipRequested()
                            }
                        }
                    }
                }
            }
        }
    }
}