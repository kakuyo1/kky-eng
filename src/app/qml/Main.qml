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
        value: Controller.settings.theme
    }

    /// @return The tray icon's rectangle, for anchoring the menu and the panels. Empty only
    ///         until the icon has been seen once: the shell reports nothing while the taskbar
    ///         is down, and an auto-hidden taskbar is down whenever a panel is opened from the
    ///         menu. Tray::geometry() remembers the last place it did report.
    function trayAnchor() {
        const g = Tray.geometry;
        if (g.width > 0)
            return g;
        return Qt.rect(Screen.virtualX + Screen.width - 56,
                       Screen.virtualY + Screen.height - 48, 56, 48);
    }

    /// @return The screen a point falls on, or this window's own when it falls on none.
    function screenFor(point) {
        // QtQml's type information carries no `screens` member on Qt.application, though the
        // real object has one, so the loop below is the one statement that has to be exempt
        // from the member check.
        // qmllint disable missing-property
        for (const s of Qt.application.screens)
            if (point.x >= s.virtualX && point.x < s.virtualX + s.width
                && point.y >= s.virtualY && point.y < s.virtualY + s.height)
                return s;
        // qmllint enable missing-property
        return screen;
    }

    /// @return Where a window of this size goes to sit beside the icon, whole.
    ///
    /// The card's bottom-right corner starts at the icon's centre: a flyout on a bottom-edge
    /// taskbar opens up and to the left of the icon that opened it. The 26 is the shadow margin
    /// every surface carries, so it is the card rather than the transparent window around it
    /// that meets the icon; the clamp is what keeps the card on screen, since the panel is
    /// wider than the icon's distance from the edge.
    function placeBeside(anchor, width, height) {
        const target = root.screenFor(anchor);
        const margin = 8;
        const left = target.virtualX + margin;
        const top = target.virtualY + margin;
        const right = target.virtualX + target.width - margin;
        const bottom = target.virtualY + target.height - margin;

        const x = Math.max(left, Math.min(anchor.x + anchor.width / 2 - width + 26, right - width));
        let y = anchor.y + anchor.height / 2 - height + 26;
        if (y < top)
            y = anchor.y + anchor.height + 8;
        return Qt.point(Math.round(x), Math.max(top, Math.min(y, bottom - height)));
    }

    /// @brief Put a surface up beside the tray icon, on whichever screen that is.
    function placePanel(panel, anchor) {
        const at = root.placeBeside(anchor, panel.width, panel.height);
        panel.x = at.x;
        panel.y = at.y;
        panel.visible = true;
        // `visible` alone is not enough. Every surface carries WindowStaysOnTopHint, but a
        // click on the tray icon activates the taskbar -- which is topmost too and re-raises
        // itself -- so a panel shown into that moment can land under it, with the bottom of
        // the card hidden behind the taskbar it was opened from. Raising is the second half
        // of showing a surface, here and in Bubble.show() / SelectionBar.openAt().
        panel.raise();
    }

    /**
     * Take down every panel but the one being opened.
     *
     * The bubble and the action bar are not panels: they arrive on their own when a selection
     * is made, and shutting them because the reader opened a menu would close what they were
     * looking at.
     */
    function hidePanels(except) {
        const panels = [trayMenu, settingsPopup, statsPopup, wordsPopup, costPopup];
        for (const panel of panels)
            if (panel !== except)
                panel.visible = false;
    }

    /// Bring the statistics panel up on its own. It is the one of its three the tray menu opens;
    /// the other two are reached from it and go back through the arrow in their own corner.
    function showStats() {
        root.hidePanels(statsPopup);
        root.placePanel(statsPopup, root.trayAnchor());
    }

    /// And the settings panel, the same way: the menu is one entry point, and picking from it
    /// should leave one thing on screen rather than stack another beside what is already there.
    function showSettings() {
        root.hidePanels(settingsPopup);
        root.placePanel(settingsPopup, root.trayAnchor());
    }

    /// The two the statistics panel drills down into, placed the same way so that following the
    /// arrow to one and back to the other does not move the card.
    function showCost() {
        root.hidePanels(costPopup);
        root.placePanel(costPopup, root.trayAnchor());
    }

    function showWords() {
        root.hidePanels(wordsPopup);
        root.placePanel(wordsPopup, root.trayAnchor());
    }

    function showTrayMenu() {
        root.hidePanels(trayMenu);
        trayMenu.contentActive = true;
        trayMenu.listVisible = false;
        root.placePanel(trayMenu, root.trayAnchor());
    }

    // Debug-only automation called by main.cpp when QT_QML_DEBUG and the profiling scenario
    // environment flag are both enabled. The sequence uses the same surface entry points as
    // the reader, but avoids a tray-coordinate dependency in repeatable profiler runs.
    function profileScenario(step) {
        if (step === 0) {
            root.showTrayMenu();
        } else if (step === 1) {
            trayMenu.listVisible = true;
        } else if (step === 2) {
            trayMenu.visible = false;
            root.showStats();
        } else if (step === 3) {
            root.showCost();
        } else if (step === 4) {
            root.showWords();
        } else if (step === 5) {
            root.showSettings();
        } else if (step === 6) {
            bubble.show({word: "profile", en: "profile run", zh: "profile run", status: "new", x: 500, y: 500});
        } else if (step === 7) {
            root.hidePanels(null);
            bubble.visible = false;
            bar.visible = false;
            Qt.quit();
        }
    }

    /**
     * Turn a position the mouse hook reported into one a window can be placed at.
     *
     * The hook reads real screen pixels; windows are placed in device-independent pixels. At
     * this machine's 125% they differ by that factor, and taking one for the other throws
     * every surface a quarter-screen off what it belongs to.
     *
     * The copy is what makes the division stick. `Controller.bubble` and its siblings are
     * QVariantMaps, and the JS object QML hands back for one takes the assignment and keeps
     * the old value -- measured on the real window, `payload.x = payload.x / ratio` left the
     * bubble reading the raw physical x, so this function spent its whole life as a no-op and
     * every surface sat at 1.25x of where it was meant to be. A freshly built object has no
     * such trouble. Fields other than x and y ride along untouched.
     */
    function toDip(payload) {
        const ratio = Screen.devicePixelRatio || 1;
        const out = {};
        for (const key in payload)
            out[key] = payload[key];
        out.x = payload.x / ratio;
        out.y = payload.y / ratio;
        return out;
    }

    /**
     * Close every surface the press missed: UI.md's "click outside closes", for the action bar
     * and the four panels (the tray menu is a native QMenu and Windows closes it itself).
     *
     * The card, not the window, is the surface: the 26px around it is shadow margin, and a
     * click there reads as a click outside.
     */
    function contains(rect: var, point: var): bool {
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
            settingsPopup.closeChild();
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
        onCostRequested: root.showCost()
        onWordsRequested: root.showWords()
    }

    CostPopup {
        id: costPopup
        onBackRequested: root.showStats()
    }

    WordsPopup {
        id: wordsPopup
        onBackRequested: root.showStats()
    }

    TrayMenu {
        id: trayMenu
        // The menu asks; the placement is here, because here is where every surface's
        // placement lives.
        onStatsRequested: root.showStats()
        onSettingsRequested: root.showSettings()
    }

    Connections {
        target: Controller

        function onSelectionBarRequested(payload) {
            bar.selectionText = payload.text;
            bar.openAt(root.toDip(payload));
        }

        function onBubbleChanged() {
            const payload = Controller.bubble;
            if (payload && Object.keys(payload).length > 0)
                bubble.show(root.toDip(payload));
        }

        function onPointerPressed(at) {
            root.dismissOutside(root.toDip(at));
        }
    }

    Connections {
        target: Tray

        function onMenuRequested() {
            // Opening the menu is one of the reader's own gestures, so it takes the screen the
            // same way picking a row from it does.
            root.showTrayMenu();
        }
    }
}
