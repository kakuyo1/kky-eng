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
    /// The padding the rows leave inside the list's card, from components/DropdownField.qml.
    readonly property int listPadding: 10

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
            // Drawn, not merely declared: the editable half of the field is an input that stands
            // in the same place with the same text, and only one of the two is ever on show.
            const button = Util.textsUnder(field)
                                 .filter((t) => t.Window.window === root.Window.window && t.visible);
            compare(button.length, 1);
            compare(button[0].text, "rare");
        }

        function test_theListIsADownByDefaultAndComesUpOnDemand() {
            const field = make({});
            let opened = 0;
            field.listOpened.connect(function () { opened += 1; });

            compare(field.openChildRect, null, "the list should start down");
            field.openList();
            verify(field.openChildRect !== null, "openList() did not raise the list");
            compare(opened, 1);
            field.closeList();
            compare(field.openChildRect, null, "closeList() left the list up");
        }

        /// Where the list lands, and where its rows land inside it. Both are in screen
        /// coordinates, so the comparison is the one the reader makes: the card sits on the
        /// field's left edge, and the first row sits inside the card and not beside it.
        ///
        /// The rows are the half that shipped wrong. `ShadowCard` hands its children to the card
        /// rather than to the window, so an inset measured from the window counted the shadow
        /// margin twice and put every row 26px right and low of the field it belongs to.
        function test_theCardLandsOnTheFieldAndTheRowsLandInsideIt() {
            const field = opened({});

            // The list window is a shadow margin bigger than its card on every side, so the card's
            // left edge is where the field's is, and its top is the six pixels under it.
            const originX = field.Window.window ? field.Window.window.x : 0;
            const originY = field.Window.window ? field.Window.window.y : 0;
            const at = field.mapToItem(null, 0, 0);
            compare(Math.round(field.openChildRect.x), Math.round(originX + at.x),
                    "the list's card is not on the field's left edge");
            compare(Math.round(field.openChildRect.y), Math.round(originY + at.y + field.height + 6),
                    "the list's card is not below the field");

            // And the rows sit inside that card, by the list's own padding and no more. Measured
            // against the card rather than the window: the card rises into place as it fades in,
            // so where it is in the window is still moving when the first row is asked for.
            const row = Util.findAll(field, (o) => o.height === field.rowHeight)[0];
            verify(row, "the list draws no row");
            const inside = row.mapToItem(Util.ofType(field, "ShadowCard").card, 0, 0);
            compare(Math.round(inside.x), root.listPadding, "the first row is not inside the card");
            compare(Math.round(inside.y), root.listPadding, "the first row is not inside the card");
        }

        /// The whole field is the target when there is nothing to type in it.
        function test_theFieldOpensTheListWhereThereIsNothingToType() {
            const field = make({});

            mouseClick(field, field.width / 2, field.height / 2);
            wait(50);
            verify(field.openChildRect !== null, "the field itself did not open the list");
        }

        /// An editable field is the reader's to type in, so the body of it is theirs and only the
        /// chevron is a button. Which half took the press is the whole shape of the control.
        function test_onlyTheChevronOpensTheListOnceTheFieldIsEditable() {
            const field = make({ editable: true });

            mouseClick(field, 20, field.height / 2);
            wait(50);
            compare(field.openChildRect, null, "a press in the text opened the list");

            mouseClick(field, field.width - 18, field.height / 2);
            wait(50);
            verify(field.openChildRect !== null, "the chevron did not open the list");
        }

        /// A field whose list is empty has nothing to open, so its chevron is dimmed and takes no
        /// press. The field itself is untouched: what the service never listed is typed by hand.
        ///
        /// This is what a service that answers 401 to `/models` looks like -- a model field the
        /// reader clicks and nothing happens, which reads as a broken control rather than as a
        /// service that has not said what it carries.
        function test_theChevronIsInertWhenThereIsNoList() {
            const field = make({ editable: true, options: [] });

            const chevron = Util.findAll(field, (o) => typeof o.source === "string"
                                                        && o.source.indexOf("chevron") >= 0)[0];
            verify(chevron, "the field draws no chevron");
            verify(chevron.opacity < 1, "the chevron of a listless field is not dimmed");

            mouseClick(field, field.width - 18, field.height / 2);
            wait(50);
            compare(field.openChildRect, null, "a chevron with no list behind it opened one");
        }

        /// Typing reports the value it was left with, and the field keeps showing what the owner
        /// answered with rather than what was typed.
        function test_typingReportsTheTextAndRebindsToTheValue() {
            const field = make({ editable: true, currentValue: "whole" });

            let typed = "";
            field.edited.connect(function (text) { typed = text; });

            const input = Util.textsUnder(field).filter((t) => t.visible && t.Window.window === root.Window.window)[0];
            verify(input, "the editable field draws nothing to type in");
            compare(input.text, "whole");

            input.forceActiveFocus();
            input.text = "a private deployment";
            root.forceActiveFocus();
            compare(typed, "a private deployment", "the edit was not reported");
            compare(input.text, "whole", "the field did not go back to the value in force");
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
