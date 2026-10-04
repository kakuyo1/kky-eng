import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The three panels that are read rather than operated: today's figures, the cost breakdown and
 * the word list.
 *
 * Each is built by one expression per figure, off a QVariantMap the controller builds, so the
 * failure worth catching is a key that does not exist -- which draws as "undefined" or as a
 * zero and reads as a quiet figure rather than as an error. The rows are reached through
 * StatRow's own label and value rather than by matching rendered strings, which is what keeps
 * these from turning into a second copy of the panels.
 */
Item {
    id: root
    width: 640
    height: 640

    Component {
        id: statsComponent
        StatsPopup {}
    }

    Component {
        id: costComponent
        CostPopup {}
    }

    Component {
        id: wordsComponent
        WordsPopup {}
    }

    TestCase {
        name: "Panels"
        when: windowShown

        function make(component) {
            const panel = createTemporaryObject(component, root);
            verify(panel);
            wait(50);
            return panel;
        }

        /// @return The panel's rows, in the order the panel declares them.
        function rowsOf(panel) {
            return Util.findAll(panel, function (o) {
                return o.toString().indexOf("StatRow_QMLTYPE") === 0;
            });
        }

        /// @return The rows' main figures, keyed by label.
        function figuresOf(panel) {
            const rows = rowsOf(panel);
            const byLabel = {};
            for (let i = 0; i < rows.length; ++i)
                byLabel[rows[i].label] = rows[i].value;
            return byLabel;
        }

        /// @return The rows' secondary figures, keyed by label; empty where a row carries none.
        function notesOf(panel) {
            const rows = rowsOf(panel);
            const byLabel = {};
            for (let i = 0; i < rows.length; ++i)
                byLabel[rows[i].label] = rows[i].note;
            return byLabel;
        }

        /// @return The one figure drawn at the hero size.
        function heroOf(panel) {
            const heroes = Util.findAll(panel, function (o) {
                return o.font !== undefined && o.font.pixelSize === 32;
            });
            compare(heroes.length, 1, "the panel should draw exactly one hero figure");
            return heroes[0].text;
        }

        function test_theStatisticsPanelShowsTodaysFigures() {
            const stats = make(statsComponent);
            const figures = figuresOf(stats);

            compare(heroOf(stats), String(Controller.stats.todayPops));
            compare(figures["Known"], String(Controller.stats.todayLearned));
            compare(figures["New"], String(Controller.stats.todayFresh));
            compare(figures["Cost"], Controller.stats.currency + Controller.stats.todayCost.toFixed(2));
            compare(figures["All time"], qsTr("%1 words").arg(Controller.words.length));
        }

        /// The other dimension the cost panel was asked for: every row still draws the amount,
        /// and the three buckets the controller prices draw the tokens behind that amount in
        /// their secondary slot. The daily average is derived, not a bucket, so it has none.
        function test_theCostPanelShowsTheSameFiguresInTheOtherDimension() {
            const cost = make(costComponent);
            const figures = figuresOf(cost);
            const notes = notesOf(cost);

            compare(heroOf(cost), Controller.cost.currency + Controller.cost.month.toFixed(2));
            compare(figures["Today"], Controller.cost.currency + Controller.cost.today.toFixed(2));
            compare(figures["Yesterday"], Controller.cost.currency + Controller.cost.yesterday.toFixed(2));
            compare(figures["This week"], Controller.cost.currency + Controller.cost.week.toFixed(2));
            compare(figures["Daily average"], Controller.cost.currency + Controller.cost.dailyAverage.toFixed(2));

            compare(notes["Today"], qsTr("%1 tokens").arg(String(Controller.cost.todayTokens)));
            compare(notes["Yesterday"], qsTr("%1 tokens").arg(String(Controller.cost.yesterdayTokens)));
            compare(notes["This week"], qsTr("%1 tokens").arg(String(Controller.cost.weekTokens)));
            compare(notes["Daily average"], "", "the daily average has no token bucket to draw");
        }

        /// @return The word each row is showing, for the rows the filter lets through.
        function wordsShown(popup) {
            const words = Controller.words.map(function (entry) { return entry.word; });
            return Util.findAll(popup, function (o) {
                return typeof o.text === "string" && o.visible && words.indexOf(o.text) >= 0;
            }).map(function (o) { return o.text; });
        }

        function test_theWordsPanelListsEveryDistinctWordTheControllerHolds() {
            const popup = make(wordsComponent);
            const words = Controller.words.map(function (entry) { return entry.word; });
            verify(words.length > 1, "the fixture needs more than one word for this to mean anything");

            compare(wordsShown(popup).sort(), words.slice().sort());
        }

        /// A delegate nested inside a Repeater inside a ListView, read through the model's own
        /// verdict rather than through the translated label the row draws.
        function test_theVerdictFilterNarrowsTheList() {
            const popup = make(wordsComponent);

            popup.filter = 1;
            wait(50);
            const known = Controller.words.filter(function (e) { return e.verdict === "known"; }).map(function (e) { return e.word; });
            compare(wordsShown(popup).sort(), known.slice().sort(), "the Known filter is not showing just the known words");

            popup.filter = 2;
            wait(50);
            const fresh = Controller.words.filter(function (e) { return e.verdict === "new"; }).map(function (e) { return e.word; });
            compare(wordsShown(popup).sort(), fresh.slice().sort(), "the New filter is not showing just the new words");
        }

        /// @return The row drawing @p word, or null when no row shows it.
        function rowFor(popup, word) {
            const text = Util.textWith(Util.textsUnder(popup), [word]);
            return text ? text.parent : null;
        }

        /// @return The pill @p row draws its @p label in.
        function pillFor(row, label) {
            const labels = Util.findAll(row, function (o) { return o.text === label; });
            compare(labels.length, 1, "a row should draw exactly one " + label + " pill");
            return labels[0].parent;
        }

        /// Marking from the list is what settles a word the reader never marked while the
        /// bubble was up. The row reports it by filling the pill of the verdict it now holds,
        /// so the case watches the fill rather than the controller behind it.
        function test_tappingAVerdictMarksTheWordAndTheRowFollows() {
            const popup = make(wordsComponent);
            popup.visible = true;

            const unmarked = Controller.words.filter(function (e) { return e.verdict === ""; });
            verify(unmarked.length > 0, "the fixture needs a word that was never marked");
            const word = unmarked[0].word;

            const row = rowFor(popup, word);
            verify(row, "the row for " + word + " is not on screen");
            verify(!Qt.colorEqual(pillFor(row, "Known").color, Tokens.ink),
                   "an unmarked word should not already show a filled Known pill");

            const known = pillFor(row, "Known");
            mouseClick(known, known.width / 2, known.height / 2);

            tryVerify(function () {
                const follow = rowFor(popup, word);
                return follow !== null && Qt.colorEqual(pillFor(follow, "Known").color, Tokens.ink);
            }, 1000, "the Known pill did not fill after the tap");

            const marked = Controller.words.filter(function (e) { return e.word === word; })[0];
            compare(marked.verdict, "known", "the tap did not reach the controller");
        }

        /// The other half of the same control: a word the reader already settled has to take a
        /// new verdict. The list reads each word's newest entry, so a pill that could only fill
        /// an entry with no verdict yet left the second press looking dead.
        function test_reMarkingAWordMovesTheRowToTheOtherVerdict() {
            const popup = make(wordsComponent);
            popup.visible = true;

            const settled = Controller.words.filter(function (e) { return e.verdict === "known"; });
            verify(settled.length > 0, "the fixture needs a word already marked known");
            const word = settled[0].word;

            const row = rowFor(popup, word);
            verify(row, "the row for " + word + " is not on screen");
            verify(Qt.colorEqual(pillFor(row, "Known").color, Tokens.ink),
                   "a known word should show a filled Known pill");

            const fresh = pillFor(row, "New");
            mouseClick(fresh, fresh.width / 2, fresh.height / 2);

            tryVerify(function () {
                const follow = rowFor(popup, word);
                return follow !== null
                    && Qt.colorEqual(pillFor(follow, "New").color, Tokens.ink)
                    && !Qt.colorEqual(pillFor(follow, "Known").color, Tokens.ink);
            }, 1000, "the row did not follow the word to its new verdict");

            // The Controller is shared by every case in the run, so put the word back where the
            // case found it rather than leaving the fixture settled the other way.
            Controller.mark(word, true);
        }

        /// The export's text half is the controller's, and lens_gtest_unit pins its shape. What
        /// is left for this case is the seam the file dialog hands its answer to: the scope the
        /// row filter maps to, and that an unknown one is refused rather than written.
        function test_theExportFollowsTheFilterAndRefusesAnyOtherScope() {
            const popup = make(wordsComponent);

            compare(popup.scope, "all", "the panel opens on the whole list");
            popup.filter = 1;
            compare(popup.scope, "known");
            popup.filter = 2;
            compare(popup.scope, "new");

            // A path under a directory that does not exist, so a write could not land even if
            // the scope check let it through.
            compare(Controller.saveWords("file:///no-such-directory/words.txt", "everything"), false);
        }
    }
}
