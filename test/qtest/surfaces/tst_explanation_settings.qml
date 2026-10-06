import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

Item {
    id: root
    width: 380
    height: 480

    readonly property var settings: ({
        levels: [{value: 2, label: "CET-4", group: "", note: ""}], level: 2,
        languages: [
            {value: "en", label: "English", group: "", note: ""},
            {value: "zh", label: "中文", group: "", note: ""},
            {value: "es", label: "Español", group: "", note: ""},
            {value: "ja", label: "日本語", group: "", note: ""}
        ],
        explanationLang: "es", multiSense: true,
        providers: [{value: "openai", label: "OpenAI", group: "International", note: ""}],
        provider: "openai", model: "gpt-4.1-mini",
        models: [
            {value: "gpt-4.1-mini", label: "gpt-4.1-mini", group: "", note: ""},
            {value: "gpt-4.1-nano", label: "gpt-4.1-nano", group: "", note: ""}
        ],
        modelPrice: "Input 0.4 / Output 1.6 USD per 1000000 tokens",
        hasApiKey: false, url: "https://api.openai.com/v1", providerDefaultUrl: "https://api.openai.com/v1"
    })

    Component { id: learning; SettingsLearningPage { width: 320; settings: root.settings } }
    Component { id: model; SettingsModelPage { width: 320; settings: root.settings } }
    Component { id: connection; SettingsConnectionPage { width: 320; settings: root.settings } }
    Component { id: backdrop; Rectangle { width: 380; height: 480; color: Tokens.panel; z: -1 } }

    TestCase {
        id: testCase
        name: "ExplanationSettings"
        when: windowShown

        function test_picksReachControllerAndRefreshCatalogBindings() {
            const previous = {
                provider: Controller.settings.provider,
                model: Controller.settings.model,
                url: Controller.settings.url,
                explanationLang: Controller.settings.explanationLang
            };
            try {
                const service = createTemporaryObject(model, root, {settings: Qt.binding(function() { return Controller.settings; })});
                verify(service);
                findChild(service, "providerField").picked("openai");
                compare(Controller.settings.provider, "openai");
                compare(Controller.settings.url, "https://api.openai.com/v1");
                const modelField = findChild(service, "modelField");
                compare(modelField.options.length, 2);
                modelField.picked("gpt-4.1-nano");
                compare(Controller.settings.model, "gpt-4.1-nano");
                compare(modelField.current.label, "gpt-4.1-nano");
                const page = createTemporaryObject(learning, root, {settings: Qt.binding(function() { return Controller.settings; })});
                verify(page);
                findChild(page, "languageField").picked("ja");
                compare(Controller.settings.explanationLang, "ja");
                compare(findChild(page, "languageField").current.label, "日本語");
                verify(Object.keys(Controller.settings).indexOf("apiKey") === -1);
            } finally {
                Controller.setProvider(previous.provider);
                Controller.setModel(previous.model);
                Controller.setApiUrl(previous.url);
                Controller.setExplanationLang(previous.explanationLang);
            }
        }

        function test_catalogControlsUseSuppliedChoices() {
            const page = createTemporaryObject(learning, root);
            verify(page);
            const field = findChild(page, "languageField");
            verify(field);
            compare(field.options.length, 4);
            compare(field.current.label, "Español");
            field.currentValue = "ja";
            compare(field.current.label, "日本語");
            const service = createTemporaryObject(model, root);
            verify(service);
            const providerField = findChild(service, "providerField");
            verify(providerField);
            compare(providerField.options.length, 1);
            compare(providerField.current.label, "OpenAI");
            const modelField = findChild(service, "modelField");
            verify(modelField);
            compare(modelField.current.label, "gpt-4.1-mini");
            compare(modelField.options.length, 2);
        }

        function test_snapshotPageContents() {
            if (!lensQaSnapshotDir) skip("Set LENS_QA_SNAPSHOT_DIR to save snapshots");
            const theme = Tokens.theme;
            try {
                for (const mode of ["light", "dark"]) {
                    Tokens.theme = mode;
                    const background = createTemporaryObject(backdrop, root);
                    verify(background);
                    for (const entry of [{component: learning, name: "learning"}, {component: model, name: "model"}, {component: connection, name: "connection"}]) {
                        const page = createTemporaryObject(entry.component, root);
                        verify(page);
                        page.x = 30;
                        page.y = 30;
                        waitForRendering(page);
                        verify(Util.saveSnapshot(testCase, root, lensQaSnapshotDir, "explanation-" + entry.name + "-" + mode));
                        verify(page.implicitHeight < root.height - 60);
                        page.destroy();
                    }
                    background.destroy();
                }
            } finally {
                Tokens.theme = theme;
            }
        }
    }
}
