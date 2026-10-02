import QtQuick
import QtQuick.Window

/**
 * The root, and the only place the surfaces are tied to the controller.
 *
 * There is no main window, and this one is not it either: it is a 1x1 transparent window kept
 * off every screen, and its only job is to own the surfaces and be the engine's root object.
 * It has to be *shown*, though -- a window declared inside another one becomes its transient
 * child, and Windows does not put a transient child on screen while its parent is hidden.
 * Every surface is a window of its own, which is what UI.md's "each surface is independent,
 * sharing no window" asks for.
 */
Window {
    id: root

    width: 1
    height: 1
    x: 0
    y: 0
    opacity: 0
    visible: true
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput

    /// The colour table follows the stored theme, whichever surface is showing.
    Binding {
        target: Tokens
        property: "theme"
        value: controller.settings.theme
    }

    /// @return Where to hang a panel: beside the tray icon, or the screen's corner when the
    ///         shell does not say where the icon is.
    function trayAnchor() {
        const g = tray.geometry;
        if (g && g.width > 0)
            return Qt.point(g.x, g.y);
        return Qt.point(screen.virtualX + screen.width - 24, screen.virtualY + screen.height - 48);
    }

    SelectionBar {
        id: bar
    }



    Bubble {
        id: bubble
    }

    SettingsPopup {
        id: settingsPopup
    }

    StatsPopup {
        id: statsPopup
        onCostRequested: costPopup.openNear(root.trayAnchor())
        onWordsRequested: wordsPopup.openNear(root.trayAnchor())
    }

    CostPopup {
        id: costPopup
    }

    WordsPopup {
        id: wordsPopup
    }

    SendConfirm {
        id: sendConfirm
    }

    Connections {
        target: controller

        function onSelectionBarRequested(payload) {
            bar.selectionText = payload.text;
            bar.openAt(payload);
        }

        function onBubbleChanged() {
            const payload = controller.bubble;
            if (payload && Object.keys(payload).length > 0)
                bubble.show(payload);
        }

        function onConfirmSendRequest(words) {
            sendConfirm.ask(words);
        }
    }

    Connections {
        target: tray

        function onStatsRequested() {
            statsPopup.openNear(root.trayAnchor());
        }

        function onSettingsRequested() {
            settingsPopup.openNear(root.trayAnchor());
        }

        function onQuitRequested() {
            Qt.quit();
        }
    }
}
