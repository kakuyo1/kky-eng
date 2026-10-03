import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The collapsed dropdown and the grouped list it opens.
 *
 * The list is a window of its own, so most of this is unreachable from the field's item tree --
 * Util.textsInNestedWindow is what crosses over. The rows are a Repeater's delegates with the
 * label, the note and the group rule nested one level in, which is the wiring docs/QML.md
 * section 8 says no linter sees.
 */
Item {
    id: root
    width: 320
    height: 480

    readonly property var options: [
        { value: 0, label: "whole", group: "one", note: "" },
        { value: 1, label: "phrase", group: "one", note: "2" },
        { value: 2, label: "rare", group: "two", note: "" },
    ]
    readonly property var labels: ["whole", "phrase", "rare"]

    Component {
        id: fieldComponent
        DropdownField {
            width: 200
            options: root.options
            currentValue: 0
        }
    }

    TestCase {
        name: "DropdownField"
        when: windowShown

        function make(properties) {
            const field = createTemporaryObject(fieldComponent, root, properties);
            verify(field);
            return field;
        }

        /// @return The field with its list open.
        function opened(properties) {
            const field = make(properties);
            field.openList();
            wait(50);
            return field;
        }

        function test_theButtonShowsTheCurrentOptionsLabel() {
            const field = make({ currentValue: 2 });

            compare(field.current.label, "rare", "the field did not resolve its current value");
            const button = Util.textsUnder(field).filter((t) => t.Window.window === root.Window.window);
            compare(button.length, 1);
            compare(button[0].text, "rare");
        }

        function test_theListIsADownByDefaultAndComesUpOnDemand() {
            const field = make({});

            compare(field.openChildRect, null, "the list should start down");
            field.openList();
            verify(field.openChildRect !== null, "openList() did not raise the list");
            field.closeList();
            compare(field.openChildRect, null, "closeList() left the list up");
        }

        function test_everyRowShowsItsLabelAndNote() {
            const field = opened({});

            const shown = Util.textsInNestedWindow(field).map((t) => t.text);
            compare(shown.sort(), ["", "", "2", "phrase", "rare", "whole"]);
        }

        function test_onlyTheSelectedRowTakesTheInkFill() {
            const field = opened({ currentValue: 2 });

            // Labels, not every text: a row's note sits on the same inked rectangle its label does.
            const labels = Util.textsInNestedWindow(field).filter((t) => root.labels.indexOf(t.text) >= 0);
            const inked = labels.filter((t) => t.parent.color === Tokens.ink);
            compare(inked.length, 1, "exactly one row should carry the ink fill");
            compare(inked[0].text, "rare");
        }

        /// The rule is `group.index > 0 && group.modelData.group !== options[index - 1].group`,
        /// so it reads both of the two things a nested delegate must qualify.
        function test_theRuleShowsOnlyWhereTheGroupChanges() {
            const field = opened({});

            const rules = Util.findAll(field, (o) => o.height === 1);
            compare(rules.length, root.options.length, "one rule per row");

            const shown = rules.filter((r) => r.visible);
            compare(shown.length, 1, "only the row whose group differs from the one before it carries a rule");
            compare(Util.textWith(Util.textsUnder(shown[0].parent), root.labels).text, "rare",
                    "the rule is on the wrong row");
        }

        function test_pickingARowReportsItsValueAndClosesTheList() {
            const field = opened({});

            let picked = -1;
            field.picked.connect(function (value) { picked = value; });

            const row = Util.textWith(Util.textsInNestedWindow(field), ["rare"]).parent;
            mouseClick(row, 20, row.height / 2);

            compare(picked, 2, "the row reported the wrong value");
            compare(field.openChildRect, null, "the list stayed up after a row was picked");
        }
    }
}
