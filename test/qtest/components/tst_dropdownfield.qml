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

    /// Options shaped the way the model field really receives them -- the id is both the `value`
    /// and the `label`, which is why the filter matches on `value` -- and with their groups
    /// interleaved on purpose. The numbering of the rows above cannot be searched at all, since
    /// their values are 0, 1 and 2; searching is what this second set is for.
    ///
    /// The groups alternate so that a search can bring two rows of the same group together. That
    /// is the case the group rule has to get right, and it is why these are not simply sorted.
    readonly property var modelOptions: [
        { value: "deepseek-v4-pro", label: "deepseek-v4-pro", group: "Domestic", note: "" },
        { value: "gpt-4.1-pro", label: "gpt-4.1-pro", group: "International", note: "" },
        { value: "gpt-4.1-mini", label: "gpt-4.1-mini", group: "International", note: "" },
        { value: "deepseek-v4-flash", label: "deepseek-v4-flash", group: "Domestic", note: "" },
    ]
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

        /// @return The editable field's input, which is where every keystroke in the cases below
        ///         has to land.
        function inputOf(field) {
            const input = Util.textsUnder(field).filter(
                (t) => t.visible && t.Window.window === root.Window.window)[0];
            verify(input, "the editable field draws nothing to type in");
            return input;
        }

        /// Search the field: the filter that follows from the text a reader would have left in it.
        ///
        /// Called through the component's own entry point rather than by typing, because nothing
        /// can type here. Under this QtTest (6.9.0, offscreen) keyPress/keyRelease, keySequence and
        /// keyClick all leave a TextInput empty, and keyClick with a printable key asserts fatally
        /// inside qasciikey.cpp and takes the whole binary down with it. Measured, not assumed --
        /// and it is why `filterTyped` takes the text as an argument.
        ///
        /// What that leaves unpinned is the one line joining the two,
        /// `onTextEdited: root.filterTyped(input.text)`. Everything behind it -- narrowing,
        /// showing, hiding, and the ways the list goes down -- is what the cases below cover.
        ///
        /// No text is the deletion case: an emptied field is one state however it got there, which
        /// is why the same call covers typing and deleting.
        function type(field, text) {
            field.filterTyped(text);
            wait(20);
        }

        /// What the reader would see in the open list: the labels of the rows that are actually
        /// drawn, in order. Every row also carries a note, which is blank on all the model-shaped
        /// options above, so the blanks are what is dropped rather than a list of expected names --
        /// a case that named them would have to be rewritten every time the fixture changed.
        function rowsShown(field) {
            return Util.textsInNestedWindow(field)
                .filter((t) => t.text !== "")
                .map((t) => t.text);
        }

        /// Typing is how the model list is searched, so the list comes up on its own: the reader
        /// types "mini" and the rows narrow to the one id carrying it, with no press on anything.
        function test_typingNarrowsTheRowsAndRaisesTheListByItself() {
            const field = make({ editable: true, currentValue: "", options: root.modelOptions });
            compare(field.openChildRect, null, "the list should start down");

            type(field, "mini");

            verify(field.openChildRect !== null, "typing did not bring the list up by itself");
            compare(rowsShown(field), ["gpt-4.1-mini"]);
            compare(field.filterText, "mini");
        }

        /// A substring is a substring, and the reader's case is not the catalogue's. The option is
        /// spelled in caps and typed in lower case, because the reader's keyboard does not know
        /// how the catalogue spells it. A subsequence match would have found nothing here, which
        /// is what "reads as a broken filter" means.
        function test_theMatchIgnoresTheCaseTheReaderTypesIn() {
            const field = make({
                editable: true,
                currentValue: "",
                options: [{ value: "GLM-5.3-PRIME", label: "GLM-5.3-PRIME", group: "", note: "" }],
            });

            type(field, "glm");

            compare(rowsShown(field), ["GLM-5.3-PRIME"]);
        }

        /// Once the text is one of the ids there is nothing left to choose from it, so the list
        /// goes away by itself -- in the reader's case as well as the catalogue's, since the
        /// catalogue spells this one in caps and no reader's keyboard does.
        function test_typingAnIdInAnyCasePutsTheListAway() {
            const field = make({
                editable: true,
                currentValue: "",
                options: [{ value: "RARE-MODEL", label: "RARE-MODEL", group: "", note: "" }],
            });

            type(field, "rare");
            verify(field.openChildRect !== null, "the list never came up");

            type(field, "rare-model");
            compare(field.openChildRect, null, "the list stayed up once the text was an id in it");
        }

        /// A search never writes to the field it is narrowing: a name the catalogue has never
        /// heard of is what this shape of field exists for, and rewriting it would move the caret
        /// out from under the reader mid-word. The text here comes from the binding the field
        /// starts with, so nothing has to be typed to ask the question.
        function test_theSearchNeverRewritesWhatIsInTheField() {
            const field = make({
                editable: true,
                currentValue: "my-own-name",
                options: root.modelOptions,
            });
            const input = inputOf(field);
            compare(input.text, "my-own-name");

            type(field, "mini");

            compare(input.text, "my-own-name", "the search rewrote what is in the field");
        }

        /// A name no id carries leaves no list: an empty card is not a dropdown. The field is still
        /// open for business afterwards, and the chevron brings the whole list back rather than the
        /// empty result the search left behind.
        function test_typingSomethingNoIdContainsLeavesNoList() {
            const field = make({ editable: true, currentValue: "", options: root.modelOptions });

            type(field, "a private deployment");

            compare(field.openChildRect, null, "a search that matched nothing opened a list");

            field.openList();
            wait(20);

            compare(rowsShown(field).length, root.modelOptions.length,
                    "the chevron did not bring the whole list back");
        }

        /// A space is not a search: it trims to nothing, which is the state an emptied field is
        /// in, so there is no list and no filter. Worth pinning because the other reading --
        /// treating the space as a needle -- shows an empty card, and a reader types a space
        /// without meaning anything by it on the way to a two-word name.
        function test_aSpaceIsNotASearchAndLeavesNoList() {
            const field = make({ editable: true, currentValue: "", options: root.modelOptions });

            type(field, " ");

            compare(field.filterText, "", "the space became a needle");
            compare(field.openChildRect, null, "a blank search opened a list");
            compare(field.shown.length, root.modelOptions.length, "the rows were narrowed");
        }

        /// Deleting back to nothing is the other way the list comes down, and it stops filtering:
        /// the rows are the whole list again, ready for the reader to press the chevron.
        function test_deletingBackToEmptyPutsTheListAwayAndStopsFiltering() {
            const field = make({ editable: true, currentValue: "", options: root.modelOptions });

            type(field, "mini");
            verify(field.openChildRect !== null, "the list never came up");
            compare(field.shown.length, 1);

            type(field, "");

            compare(field.filterText, "", "the filter survived deleting every character");
            compare(field.shown.length, root.modelOptions.length, "the rows are still narrowed");
            compare(field.openChildRect, null, "the list stayed up with nothing typed");
        }

        /// `listOpened` means the reader asked for the list, and the settings page wires it to a
        /// refresh. A keystroke is a search, not a request for one, so it must never raise it --
        /// otherwise every character typed would cost a GET.
        function test_listOpenedFiresForThePressAndNotForTyping() {
            const field = make({ editable: true, currentValue: "", options: root.modelOptions });
            let opened = 0;
            field.listOpened.connect(function () { opened += 1; });

            type(field, "mini");
            verify(field.openChildRect !== null, "typing did not bring the list up");
            compare(opened, 0, "typing raised listOpened, which the settings page reads as a request");

            field.openList();
            compare(opened, 1, "the chevron did not raise listOpened");
        }

        /// The rule above a row is drawn from the row before it in the list the view is built over.
        /// "gpt" leaves the two International rows together, so a correctly read list draws no
        /// rule at all -- while the unfiltered list would put one above the first of them, because
        /// the row above it there is the Domestic one. That gap is the whole case.
        function test_theGroupRuleFollowsTheFilteredList() {
            const field = make({ editable: true, currentValue: "", options: root.modelOptions });

            type(field, "gpt");

            verify(field.openChildRect !== null, "the list never came up");
            compare(rowsShown(field), ["gpt-4.1-pro", "gpt-4.1-mini"]);
            const shown = Util.findAll(field, (o) => o.height === 1).filter((rule) => rule.visible);
            compare(shown.length, 0,
                    "a rule was drawn between two rows of the same group, from the unfiltered list");
        }

        /// Picking a row reports it and puts the list away, search and all -- so the chevron
        /// afterwards opens the whole list rather than the fragment just picked from.
        function test_pickingARowDropsTheSearchWithTheList() {
            const field = make({ editable: true, currentValue: "", options: root.modelOptions });
            let picked = "";
            field.picked.connect(function (value) { picked = value; });

            type(field, "mini");
            verify(field.openChildRect !== null, "the list never came up");
            const row = Util.textWith(Util.textsInNestedWindow(field), ["gpt-4.1-mini"]).parent;
            mouseClick(row, 20, row.height / 2);

            compare(picked, "gpt-4.1-mini", "the row reported the wrong value");
            compare(field.openChildRect, null, "the list stayed up after a row was picked");
            compare(field.filterText, "", "the search outlived the list it narrowed");
        }

        /// A whole search writes nothing: `edited` is the field reporting what the reader left in
        /// it when they leave it, and `listOpened` is the settings page's cue to fetch. Neither is
        /// a search's business, and a request per character is what raising the second would cost.
        function test_aSearchRaisesNoEditAndNoRequest() {
            const field = make({ editable: true, currentValue: "", options: root.modelOptions });
            let edited = -1;
            let opened = 0;
            field.edited.connect(function (text) { edited = text.length; });
            field.listOpened.connect(function () { opened += 1; });

            type(field, "mini");
            type(field, "gpt");

            compare(edited, -1, "a search reported an edit");
            compare(opened, 0, "a search asked for the list");
        }
    }
}
