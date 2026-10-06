import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

Item {
    id: root
    width: 800
    height: 600

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        name: "Phase2E"
        when: windowShown

        function makeMain() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(25);
            return main;
        }

        function test_threeBuiltInThemesHaveIndependentTables() {
            compare(Tokens.themeName(0), "light");
            compare(Tokens.themeName(1), "dark");
            compare(Tokens.themeName(2), "forest");
            verify(Tokens.lightTheme.bg !== Tokens.darkTheme.bg);
            verify(Tokens.darkTheme.bg !== Tokens.forestTheme.bg);
            verify(Tokens.validateCustomTheme(Tokens.colorsForTheme("light")).valid);
            verify(Tokens.validateCustomTheme(Tokens.colorsForTheme("dark")).valid);
            verify(Tokens.validateCustomTheme(Tokens.colorsForTheme("forest")).valid);
        }

        function test_colorConversionKeepsAlphaForThemeValues() {
            const light = Tokens.colorsForTheme("light");
            const forest = Tokens.colorsForTheme("forest");
            verify(light.bubbleBg.length === 9, "light bubble alpha was dropped");
            verify(forest.bubbleBg.length === 9, "forest bubble alpha was dropped");

            const encoded = Tokens.customThemeValue(light);
            const decoded = Tokens.colorsForTheme(encoded);
            compare(decoded.bubbleBg, light.bubbleBg);
            compare(decoded.bubbleBorder, light.bubbleBorder);
        }

        function test_customTextRolesMustEachPassAa() {
            const colors = Tokens.colorsForTheme("light");
            colors.panel = "#ffffff";
            colors.text = "#ffffff";
            colors.muted = "#777777";
            colors.faint = "#999999";
            verify(!Tokens.validateCustomTheme(colors).valid);

            colors.panel = "#202020";
            colors.text = "#ffffff";
            colors.muted = "#d0d0d0";
            colors.faint = "#aaaaaa";
            const result = Tokens.validateCustomTheme(colors);
            verify(result.valid);
            verify(result.bodyRatio >= 4.5);
            verify(result.mutedRatio >= 4.5);
            verify(result.faintRatio >= 4.5);
        }

        function test_alphaTextIsValidatedAfterCompositing() {
            const colors = Tokens.colorsForTheme("light");
            colors.panel = "#202020";
            colors.text = "#20ffffff";
            colors.muted = "#20ffffff";
            colors.faint = "#20ffffff";
            verify(!Tokens.validateCustomTheme(colors).valid,
                   "transparent text must not pass AA using its uncomposited RGB values");
        }

        function test_customThemeRoundTripsThroughTheThemeSettingValue() {
            const colors = Tokens.colorsForTheme("forest");
            colors.panel = "#202020";
            colors.text = "#ffffff";
            colors.muted = "#d0d0d0";
            colors.faint = "#aaaaaa";
            const encoded = Tokens.customThemeValue(colors);
            const decoded = Tokens.colorsForTheme(encoded);

            compare(Tokens.themeIndex(encoded), 3);
            compare(decoded.panel, "#202020");
            compare(decoded.text, "#ffffff");
            compare(decoded.muted, "#d0d0d0");
            compare(decoded.faint, "#aaaaaa");
            compare(decoded.bubbleBg, colors.bubbleBg);
        }

        function test_customThemeIsReadBackAfterBeingPersisted() {
            makeMain();
            const previous = Controller.settings.theme;
            const colors = Tokens.colorsForTheme("dark");
            colors.panel = "#202020";
            colors.text = "#ffffff";
            colors.muted = "#d0d0d0";
            colors.faint = "#aaaaaa";
            const encoded = Tokens.customThemeValue(colors);

            Controller.setTheme(encoded);
            wait(25);
            compare(Controller.settings.theme, encoded);
            compare(Tokens.theme, encoded);

            verify(makeMain());
            compare(Tokens.theme, Controller.settings.theme);
            Controller.setTheme(previous);
        }

        function test_generalSettingsExposeForestAndCustomChoices() {
            const main = makeMain();
            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel);
            main.showSettings();
            panel.openCategory("general");
            wait(25);

            const page = Util.ofType(panel, "SettingsGeneralPage");
            verify(page);
            const themes = Util.findAll(page, function (o) {
                return o.objectName === "themeField";
            });
            verify(themes.length === 1, "the General page has no theme control");
            compare(themes[0].options.length, 4);
            compare(themes[0].options[2].label, "Forest");
            compare(themes[0].options[3].label, "Custom");
        }

        function test_transientPlacementReadsBubbleSizeAfterContentChanges() {
            const main = makeMain();
            const bubble = Util.ofType(main, "Bubble");
            verify(bubble);
            const anchor = Qt.point(500, 500);

            bubble.show({title: "short", type: "word", en: "short", zh: "短", x: 500, y: 500});
            wait(25);
            main.placeTransient(bubble, anchor, bubble.gap);
            const firstHeight = bubble.height;
            const firstY = bubble.y;

            bubble.show({title: "long", type: "word", senses: [
                {en: "a longer explanation that wraps", text: "第一条"},
                {en: "another explanation that wraps", text: "第二条"},
                {en: "a third explanation that wraps", text: "第三条"}
            ], x: 500, y: 500});
            wait(25);
            main.placeTransient(bubble, anchor, bubble.gap);

            verify(bubble.height > firstHeight);
            verify(bubble.y < firstY, "placement must use the current height, not the first height");
        }

        function test_narrowScreenUsesBoundsWithoutScalingContent() {
            const main = makeMain();
            const screen = {virtualX: -320, virtualY: 40, width: 320, height: 480};
            const layout = main.layoutFor(screen, 432, 532);

            verify(layout.scale === undefined, "narrow layout must not scale the content tree");
            verify(layout.width <= screen.width - 16);
            verify(layout.height <= screen.height - 16);

            const anchor = Qt.rect(-2, 90, 24, 24);
            const at = main.placeBeside(anchor, layout.width, layout.height, screen);
            verify(at.x >= screen.virtualX + 8);
            verify(at.y >= screen.virtualY + 8);
            verify(at.x + layout.width <= screen.virtualX + screen.width - 8);
            verify(at.y + layout.height <= screen.virtualY + screen.height - 8);
        }
    }
}
