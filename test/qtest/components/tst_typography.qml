import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * Mixed Chinese and Latin text must keep the application's font fallback chain.
 *
 * Monospace is appropriate for numeric-only figures, but assigning a single monospace family
 * to a mixed run makes its Chinese glyphs bypass the QFont family list installed by main.cpp.
 *
 * The last case is also the worked example of the silent (offscreen) snapshot: it renders the
 * fixture with TestCase.grabImage and writes a PNG when LENS_QA_SNAPSHOT_DIR is set. See
 * TEST.md section 5 and scripts/qml-snapshot.ps1.
 */
Item {
    id: root
    width: 320
    height: 120

    Component {
        id: statRowComponent
        StatRow { width: 240; label: "All time"; value: "128 词" }
    }

    Component {
        id: menuRowComponent
        MenuRow { width: 240; label: "Statistics"; note: "128 词 · ¥0.42" }
    }

    Column {
        id: capture
        spacing: 8
        Text { text: "Settings"; font.pixelSize: 14; font.weight: Font.Bold }
        Text { text: "设置"; font.pixelSize: 14; font.weight: Font.Bold }
        Text { text: "Vocabulary level"; font.pixelSize: 12 }
        Text { text: "词汇档位"; font.pixelSize: 12 }
        MenuRow { width: 260; label: "今日统计"; note: "14 词 · ¥0.00" }
        StatRow { width: 260; label: "统计"; value: "14" }
        Segment { width: 260; height: 34; labels: ["English", "中文"]; currentIndex: 0 }
    }

    TestCase {
        id: testCase
        name: "Typography"
        when: windowShown

        function make(component, properties) {
            const item = createTemporaryObject(component, root, properties || {});
            verify(item);
            wait(20);
            return item;
        }

        function mixedValueOf(row, value) {
            const values = Util.findAll(row, (o) => o.value === value);
            compare(values.length, 1);
            return values[0];
        }

        function test_mixedStatValueInheritsTheUiFont() {
            const row = make(statRowComponent);
            const value = mixedValueOf(row, "128 词");
            compare(value.runs.length, 2);
            compare(value.runs[0].cjk, false);
            compare(value.runs[1].cjk, true);
        }

        function test_mixedMenuNoteInheritsTheUiFont() {
            const row = make(menuRowComponent);
            const note = mixedValueOf(row, "128 词 · ¥0.42");
            compare(note.runs.length, 3);
            compare(note.runs[0].cjk, false);
            compare(note.runs[1].cjk, true);
            compare(note.runs[2].cjk, false);
        }

        function test_numericFiguresKeepTheirMonospaceFace() {
            const row = make(statRowComponent, { value: "128" });
            const value = mixedValueOf(row, "128");
            compare(value.runs.length, 1);
            compare(value.runs[0].cjk, false);
            const rendered = Util.textWith(Util.textsUnder(row), ["128"]);
            verify(rendered);
            compare(rendered.font.family, Tokens.monoFamily);
        }

        function test_captureChineseAndLatinTypography() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qml-snapshot.ps1) to save the offscreen snapshot");

            wait(100);
            verify(Util.saveSnapshot(testCase, capture, lensQaSnapshotDir, "typography"));
        }
    }
}
