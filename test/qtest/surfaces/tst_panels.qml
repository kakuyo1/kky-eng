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
    }
}
