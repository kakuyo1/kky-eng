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

    /// Bring the statistics panel up on its own. It is the one of its three the tray menu opens;
    /// the other two are reached from it and go back through the arrow in their own corner.
    function showStats() {
        settingsPopup.visible = false;
        wordsPopup.visible = false;
        costPopup.visible = false;
        statsPopup.openNear(root.trayAnchor());
    }

    /// And the settings panel, the same way: the menu is one entry point, and picking from it
    /// should leave one thing on screen rather than stack another beside what is already there.
    function showSettings() {
        statsPopup.visible = false;
        wordsPopup.visible = false;
        costPopup.visible = false;
        settingsPopup.openNear(root.trayAnchor());
    }

    /**
     * Turn a position the mouse hook reported into one a window can be placed at.
     *
     * The hook reads real screen pixels; windows are placed in device-independent pixels. At
     * this machine's 125% they differ by that factor, and taking one for the other threw the
     * action bar a quarter-screen off its own selection. Fields other than x and y ride along
     * untouched.
     */
    function toDip(payload) {
        const ratio = screen.devicePixelRatio || 1;
        payload.x = payload.x / ratio;
        payload.y = payload.y / ratio;
        return payload;
    }

    /**
     * Close every surface the press missed: UI.md's "click outside closes", for the action bar
     * and the four panels (the tray menu is a native QMenu and Windows closes it itself).
     *
     * The card, not the window, is the surface: the 26px around it is shadow margin, and a
     * click there reads as a click outside.
     */
    function contains(rect, point) {
        return point.x >= rect.x && point.x <= rect.x + rect.width
            && point.y >= rect.y && point.y <= rect.y + rect.height;
    }

    function dismissOutside(point) {
        // A window a surface opened -- today, the settings panel's level list -- takes the
        // press first: inside it, nothing closes; outside it, it goes, and the press is then
        // judged against the surfaces like any other.
        const child = settingsPopup.visible ? settingsPopup.openChildRect : null;
        if (child) {
            if (contains(child, point))
                return;
            settingsPopup.levelField.closeList();
        }

        const surfaces = [bar, settingsPopup, statsPopup, wordsPopup, costPopup, trayMenu];
        for (const surface of surfaces) {
            if (!surface.visible)
                continue;
            const margin = surface.shadowMargin;
            // A surface may name the region that counts as a press on it: the tray menu's card
            // is not its whole surface, because the language list unfolds beside it.
            const card = surface.hitRect !== undefined
                       ? surface.hitRect
                       : Qt.rect(surface.x + margin, surface.y + margin,
                                 surface.width - 2 * margin, surface.height - 2 * margin);
            if (!contains(card, point))
                surface.visible = false;
        }
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
        // One of the three at a time: the two below replace the panel they are opened from,
        // and each carries a back arrow that brings it back.
        onCostRequested: {
            statsPopup.visible = false;
            costPopup.openNear(root.trayAnchor());
        }
        onWordsRequested: {
            statsPopup.visible = false;
            wordsPopup.openNear(root.trayAnchor());
        }
    }

    CostPopup {
        id: costPopup
        onBackRequested: root.showStats()
    }

    WordsPopup {
        id: wordsPopup
        onBackRequested: root.showStats()
    }

    SendConfirm {
        id: sendConfirm
    }

    TrayMenu {
        id: trayMenu
    }

    Connections {
        target: controller

        function onSelectionBarRequested(payload) {
            bar.selectionText = payload.text;
            bar.openAt(root.toDip(payload));
        }

        function onBubbleChanged() {
            const payload = controller.bubble;
            if (payload && Object.keys(payload).length > 0)
                bubble.show(root.toDip(payload));
        }

        function onPointerPressed(at) {
            root.dismissOutside(root.toDip(at));
        }

        function onConfirmSendRequest(words) {
            sendConfirm.ask(words);
        }
    }

    Connections {
        target: tray

        function onMenuRequested() {
            trayMenu.openAt(tray.geometry);
        }
    }
}
