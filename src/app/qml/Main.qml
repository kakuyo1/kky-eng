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

    readonly property int screenMargin: 8
    property var placements: []
    property string screenLayoutSignature: ""
    property int selectionBarGeneration: 0

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

    /// @return The screen a DIP point falls on, or the nearest screen when it falls between monitors.
    function screenFor(point) {
        let nearest = screen;
        let nearestDistance = Number.MAX_VALUE;
        // qmllint disable missing-property
        for (const s of Qt.application.screens) {
            if (point.x >= s.virtualX && point.x < s.virtualX + s.width
                && point.y >= s.virtualY && point.y < s.virtualY + s.height)
                return s;
            const dx = point.x < s.virtualX ? s.virtualX - point.x
                                             : Math.max(0, point.x - (s.virtualX + s.width));
            const dy = point.y < s.virtualY ? s.virtualY - point.y
                                             : Math.max(0, point.y - (s.virtualY + s.height));
            const distance = dx * dx + dy * dy;
            if (distance < nearestDistance) {
                nearest = s;
                nearestDistance = distance;
            }
        }
        // qmllint enable missing-property
        return nearest;
    }

    /// @return The screen containing a native-pixel point, using each screen's own scale factor.
    function screenForPhysical(point) {
        let nearest = screen;
        let nearestDistance = Number.MAX_VALUE;
        // qmllint disable missing-property
        for (const s of Qt.application.screens) {
            const ratio = s.devicePixelRatio || 1;
            const left = s.virtualX * ratio;
            const top = s.virtualY * ratio;
            const right = left + s.width * ratio;
            const bottom = top + s.height * ratio;
            if (point.x >= left && point.x < right && point.y >= top && point.y < bottom)
                return s;
            const dx = point.x < left ? left - point.x : Math.max(0, point.x - right);
            const dy = point.y < top ? top - point.y : Math.max(0, point.y - bottom);
            const distance = dx * dx + dy * dy;
            if (distance < nearestDistance) {
                nearest = s;
                nearestDistance = distance;
            }
        }
        // qmllint enable missing-property
        return nearest;
    }

    function anchorPoint(anchor) {
        return anchor.width === undefined
                ? anchor
                : Qt.point(anchor.x + anchor.width / 2, anchor.y + anchor.height / 2);
    }

    function rememberPlacement(surface, anchor, kind) {
        for (const placement of placements) {
            if (placement.surface === surface) {
                placement.anchor = anchor;
                placement.kind = kind;
                return placement;
            }
        }
        const placement = {surface: surface, anchor: anchor, kind: kind};
        placements.push(placement);
        return placement;
    }

    /// @return A native-size surface bounded to the monitor without changing its content scale.
    function layoutFor(target, width, height) {
        const availableWidth = Math.max(1, target.width - 2 * screenMargin);
        const availableHeight = Math.max(1, target.height - 2 * screenMargin);
        return {
            width: Math.min(Math.max(1, width), availableWidth),
            height: Math.min(Math.max(1, height), availableHeight)
        };
    }

    function clamp(value, low, high) {
        const upper = Math.max(low, high);
        return Math.max(low, Math.min(value, upper));
    }

    /// @return Where a window of this size goes to sit beside the icon, whole when possible.
    function placeBeside(anchor, width, height, target, shadowMargin) {
        target = target || root.screenFor(root.anchorPoint(anchor));
        shadowMargin = shadowMargin === undefined ? 26 : shadowMargin;
        const margin = root.screenMargin;
        const left = target.virtualX + margin;
        const top = target.virtualY + margin;
        const right = target.virtualX + target.width - margin;
        const bottom = target.virtualY + target.height - margin;

        const x = root.clamp(anchor.x + anchor.width / 2 - width + shadowMargin,
                             left, right - width);
        let y = anchor.y + anchor.height / 2 - height + shadowMargin;
        if (y < top)
            y = anchor.y + anchor.height + 8;
        return Qt.point(Math.round(x), Math.round(root.clamp(y, top, bottom - height)));
    }

    /// @brief Put a surface up beside the tray icon, on whichever screen that is.
    function placePanel(panel, anchor) {
        root.rememberPlacement(panel, anchor, "panel");
        const target = root.screenFor(root.anchorPoint(anchor));
        const layout = root.layoutFor(target, panel.width, panel.height);
        const at = root.placeBeside(anchor, layout.width, layout.height, target, panel.shadowMargin);
        panel.x = at.x;
        panel.y = at.y;
        panel.visible = true;
        // `visible` alone is not enough. Every surface carries WindowStaysOnTopHint, but a
        // click on the tray icon activates the taskbar -- which is topmost too and re-raises
        // itself -- so a panel shown into that moment can land under it.
        panel.raise();
    }

    // qmllint disable missing-property
    function placeTransient(surface, anchor, gap) {
        root.rememberPlacement(surface, anchor, "transient");
        const target = root.screenFor(root.anchorPoint(anchor));
        // Read the dimensions at placement time. Bubble's implicit height changes when its
        // senses, translation, or hover actions settle; caching its first size breaks that path.
        const layout = root.layoutFor(target, surface.width, surface.height);
        const left = target.virtualX + screenMargin;
        const top = target.virtualY + screenMargin;
        const right = target.virtualX + target.width - screenMargin;
        const bottom = target.virtualY + target.height - screenMargin;
        const shadow = surface.shadowMargin;
        const x = root.clamp(anchor.x - shadow, left, right - layout.width);
        const above = anchor.y - gap - layout.height + shadow;
        const below = anchor.y + gap - shadow;
        const y = root.clamp(above >= top ? above : below, top, bottom - layout.height);
        surface.x = Math.round(x);
        surface.y = Math.round(y);
        surface.raise();
    }
    // qmllint enable missing-property

    function repositionVisible() {
        for (const placement of placements) {
            if (!placement.surface.visible)
                continue;
            if (placement.kind === "panel")
                root.placePanel(placement.surface, placement.anchor);
            else if (!placement.surface.dragging)
                root.placeTransient(placement.surface, placement.anchor, placement.surface.gap);
        }
    }

    function currentScreenLayoutSignature() {
        let signature = "";
        // qmllint disable missing-property
        for (const s of Qt.application.screens)
            signature += `${s.virtualX},${s.virtualY},${s.width},${s.height},${s.devicePixelRatio || 1};`;
        // qmllint enable missing-property
        return signature;
    }

    function checkScreenLayout() {
        const signature = root.currentScreenLayoutSignature();
        if (signature !== root.screenLayoutSignature) {
            root.screenLayoutSignature = signature;
            root.repositionVisible();
        }
    }

    /**
     * Take down every panel but the one being opened.
     */
    function hidePanels(except) {
        const panels = [trayMenu, settingsPopup, statsPopup, wordsPopup, yearWordsPopup, costPopup];
        for (const panel of panels)
            if (panel !== except)
                panel.visible = false;
    }

    function showStats() {
        root.hidePanels(statsPopup);
        root.placePanel(statsPopup, root.trayAnchor());
    }

    function showSettings() {
        root.hidePanels(settingsPopup);
        root.placePanel(settingsPopup, root.trayAnchor());
    }

    function showCost() {
        root.hidePanels(costPopup);
        root.placePanel(costPopup, root.trayAnchor());
    }

    function showWords() {
        root.hidePanels(wordsPopup);
        root.placePanel(wordsPopup, root.trayAnchor());
    }

    function showYearWords() {
        root.hidePanels(yearWordsPopup);
        root.placePanel(yearWordsPopup, root.trayAnchor());
    }

    function showTrayMenu() {
        root.hidePanels(trayMenu);
        trayMenu.contentActive = true;
        trayMenu.listVisible = false;
        root.placePanel(trayMenu, root.trayAnchor());
    }

    Timer {
        interval: 250
        repeat: true
        running: true
        onTriggered: root.checkScreenLayout()
    }

    // Debug-only automation called by main.cpp when QT_QML_DEBUG and the profiling scenario
    // environment flag are both enabled.
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
            bubble.show({title: "profile", type: "word", ipa: "/ˈproʊfaɪl/", en: "profile run", zh: "profile run", status: "new", x: 500, y: 500});
        } else if (step === 7) {
            root.hidePanels(null);
            bubble.visible = false;
            bar.visible = false;
            Qt.quit();
        }
    }

    /**
     * Turn a position the mouse hook reported into one a window can be placed at.
     * The hook reads native screen pixels; windows and QScreen geometry use DIPs.
     */
    function toDip(payload) {
        const target = root.screenForPhysical(Qt.point(payload.x, payload.y));
        const ratio = target.devicePixelRatio || 1;
        const out = {};
        for (const key in payload)
            out[key] = payload[key];
        // Convert relative to the selected screen so negative virtual coordinates remain on the
        // same screen and mixed-DPI monitors do not use the root screen's scale factor.
        out.x = target.virtualX + (payload.x - target.virtualX * ratio) / ratio;
        out.y = target.virtualY + (payload.y - target.virtualY * ratio) / ratio;
        return out;
    }

    // qmllint disable missing-property
    function contains(rect, point) {
        return point.x >= rect.x && point.x <= rect.x + rect.width
            && point.y >= rect.y && point.y <= rect.y + rect.height;
    }
    // qmllint enable missing-property

    function dismissOutside(point) {
        // A panel with a surface of its own out claims the presses that land on that surface. One
        // that is not modal -- the settings dropdown -- is put down by a press anywhere else, and
        // the press is then read again below, where a press outside the panel closes the panel
        // too. A modal one, the removal question, is a question: a press elsewhere is not an
        // answer to it, and nothing here is read past it.
        for (const panel of [settingsPopup, wordsPopup]) {
            if (!panel.visible || !panel.openChildRect)
                continue;
            if (contains(panel.openChildRect, point) || panel.openChildIsModal)
                return;
            panel.closeChild();
        }

        const surfaces = [bar, settingsPopup, statsPopup, wordsPopup, yearWordsPopup, costPopup, trayMenu];
        for (const surface of surfaces) {
            if (!surface.visible)
                continue;
            const margin = surface.shadowMargin;
            const card = surface.hitRect !== undefined
                       ? surface.hitRect
                       : Qt.rect(surface.x + margin, surface.y + margin,
                                 surface.width - 2 * margin, surface.height - 2 * margin);
            if (!contains(card, point))
                surface.visible = false;
        }
    }

    SelectionBar { id: bar }
    Bubble { id: bubble }

    Notice {
        id: notice
        // The one action a notice can carry: the word it is about is the controller's, and so is
        // what asking for it costs.
        onActionTriggered: Controller.explainLemma(notice.noticeLemma)
    }

    /// The capture mask, raised by the global trigger key and by nothing else. What it gives back is
    /// a rectangle for the capture duty; that it is also a surface is why it is here and not in the
    /// settings panel that owns the key.
    Mask {
        id: mask
        // The one place the mask and the capture duty meet: the rectangle comes back in desktop
        // coordinates and goes straight to the only thing that can take a shot of it. Nothing else
        // in the mask knows a controller exists, and the controller never learns a mask does --
        // which is why this wire had no test over it, and was missing until the reader pressed the
        // key and framed a region and nothing whatsoever happened.
        onRegionChosen: (region) => Controller.captureRegion(region)
    }

    SettingsPopup { id: settingsPopup }

    StatsPopup {
        id: statsPopup
        onCostRequested: root.showCost()
        onWordsRequested: root.showWords()
    }

    CostPopup {
        id: costPopup
        onBackRequested: root.showStats()
    }

    WordsPopup {
        id: wordsPopup
        onYearRequested: root.showYearWords()
        onBackRequested: root.showStats()
    }

    YearWordsPopup {
        id: yearWordsPopup
        onBackRequested: root.showWords()
        onPaletteOpenChanged: if (visible) root.placePanel(yearWordsPopup, root.trayAnchor())
    }

    TrayMenu {
        id: trayMenu
        onStatsRequested: root.showStats()
        onSettingsRequested: root.showSettings()
    }

    Connections {
        target: Controller

        function onSelectionBarRequested(payload) {
            const at = root.toDip(payload);
            const generation = ++root.selectionBarGeneration;
            bar.selectionText = payload.text;
            bar.openAt(at);
            bar.visible = false;
            Qt.callLater(() => {
                if (generation !== root.selectionBarGeneration)
                    return;
                root.placeTransient(bar, at, bar.gap);
                bar.visible = true;
            });
        }

        function onBubbleChanged() {
            const payload = Controller.bubble;
            if (payload && Object.keys(payload).length > 0) {
                const at = root.toDip(payload);
                bubble.show(at);
                Qt.callLater(() => root.placeTransient(bubble, at, bubble.gap));
            } else {
                bubble.visible = false;
            }
        }

        function onCaptureMaskRequested() {
            mask.open();
        }

        function onNoticeChanged() {
            const payload = Controller.notice;
            if (payload && Object.keys(payload).length > 0)
                notice.show(payload);
            else
                notice.visible = false;
        }

        function onPointerPressed(at) {
            ++root.selectionBarGeneration;
            root.dismissOutside(root.toDip(at));
        }
    }

    Connections {
        target: Tray
        function onMenuRequested() {
            root.showTrayMenu();
        }
    }
}
