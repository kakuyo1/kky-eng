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

    Component {
        id: yearWordsComponent
        YearWordsPopup {}
    }

    TestCase {
        id: testCase
        name: "Panels"
        when: windowShown
        property int titleRequests: 0

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

        function test_theWordsTitleRequestsTheYearPanel() {
            const popup = make(wordsComponent);
            titleRequests = 0;
            popup.yearRequested.connect(function () { titleRequests += 1; });
            const title = Util.textWith(Util.textsUnder(popup), [qsTr("Words")]);
            verify(title, "the Words title is not drawn");

            mouseClick(title, title.width / 2, title.height / 2);
            tryCompare(testCase, "titleRequests", 1);
        }

        function test_theYearWordsPanelProjectsTheControllerDaysAndPalette() {
            const popup = make(yearWordsComponent);
            popup.visible = true;
            wait(50);

            compare(popup.dataCellCount, Controller.yearDays.length);
            compare(popup.dataCellCount, 365);
            compare(popup.gridCellCount, 371);
            verify(Controller.yearDays.some(function (day) { return day.pops === 0; }));
            const compactHeight = popup.height;

            const monthLabels = Util.findAll(popup, function (item) {
                return item.objectName === "yearMonthLabel";
            }).sort(function (left, right) {
                return left.x - right.x;
            });
            for (let i = 1; i < monthLabels.length; ++i) {
                verify(monthLabels[i - 1].x + monthLabels[i - 1].width <= monthLabels[i].x);
            }

            const selectedDay = Controller.yearDays.find(function (day) { return day.pops > 0; });
            const selectedCell = Util.findAll(popup, function (item) {
                return item.day && item.day.date === selectedDay.date;
            })[0];
            verify(selectedCell, "the fixture has no cell for a day with lookups");
            mouseClick(selectedCell, selectedCell.width / 2, selectedCell.height / 2);
            compare(popup.selectedDate, selectedDay.date);
            const selectedDayText = qsTr("%1: %2 lookups").arg(selectedDay.date).arg(selectedDay.pops);
            verify(Util.textWith(Util.textsUnder(popup), [selectedDayText]),
                   "the selected day's lookup count is not shown as text");

            popup.openPalette(3);
            tryCompare(popup, "paletteOpen", true);
            tryCompare(popup, "paletteReady", true);
            compare(popup.selectedLevel, 3);

            const wheel = Util.findAll(popup, function (item) { return item.objectName === "yearHueWheel"; })[0];
            const shades = Util.findAll(popup, function (item) { return item.objectName === "yearShadeColumn"; })[0];
            const title = Util.findAll(popup, function (item) { return item.objectName === "yearPaletteTitle"; })[0];
            const hueName = Util.findAll(popup, function (item) { return item.objectName === "yearPaletteHueName"; })[0];
            const close = Util.findAll(popup, function (item) { return item.objectName === "yearPaletteClose"; })[0];
            const header = Util.findAll(popup, function (item) { return item.objectName === "yearPaletteHeader"; })[0];
            verify(wheel && shades && title && hueName && close && header);
            verify(wheel.x + wheel.width <= shades.x);
            verify(title.x + title.width < hueName.x);
            verify(hueName.mapToItem(header, hueName.width, 0).x <= close.x);

            const green = popup.cellColor(1);
            popup.selectHue(7);
            compare(popup.hueIndex, 7);
            verify(!Qt.colorEqual(green, popup.cellColor(1)));

            popup.closePalette();
            compare(popup.paletteOpen, false);

            popup.openPalette(3);
            tryCompare(popup, "paletteReady", true);
            tryVerify(function () { return popup.height > compactHeight; });
            popup.closePalette();
            tryCompare(popup, "paletteOpen", false);
            tryCompare(popup, "height", compactHeight);

            if (lensQaSnapshotDir) {
                const wasTheme = Tokens.theme;
                popup.visible = true;
                wait(Tokens.motion.pop + 80);
                verify(Util.saveSnapshot(testCase, popup.contentItem, lensQaSnapshotDir, "year-words-light"));
                popup.openPalette(2);
                tryCompare(popup, "paletteReady", true);
                wait(Tokens.motion.pop + 80);
                verify(Util.saveSnapshot(testCase, popup.contentItem, lensQaSnapshotDir, "year-words-palette"));
                Tokens.theme = "dark";
                wait(Tokens.motion.pop + 80);
                verify(Util.saveSnapshot(testCase, popup.contentItem, lensQaSnapshotDir, "year-words-dark"));
                popup.closePalette();
                tryCompare(popup, "height", compactHeight);
                wait(Tokens.motion.pop + 80);
                verify(Util.saveSnapshot(testCase, popup.contentItem, lensQaSnapshotDir, "year-words-closed"));
                Tokens.theme = wasTheme;
                popup.visible = false;
            }
        }

        /// The card is sized to its widest row, so the columns beside the word have to fit under
        /// the word rather than over it: a word rendered narrower than it measures is one the
        /// reader sees cut short, which is the whole thing the width exists to prevent.
        function test_theWidestWordIsRenderedWhole() {
            const popup = make(wordsComponent);
            popup.visible = true;

            // The fixture carries a word long enough to outgrow the panel's base width, so a
            // card that stayed at the base would fail the comparison below rather than pass it.
            const widest = Controller.words.slice().sort(function (a, b) {
                return b.word.length - a.word.length;
            })[0].word;
            verify(popup.cardWidth > popup.minCardWidth,
                   "the widest word did not widen the card past its base");

            const row = rowFor(popup, widest);
            verify(row, "the row for " + widest + " is not on screen");

            const word = Util.findAll(row, function (o) { return o.text === widest; })[0];
            verify(word, "the row does not draw the word");
            compare(word.implicitWidth <= word.width, true,
                    "the word column is narrower than the word it draws");
            compare(word.elide, Text.ElideRight);
        }

        /// @return Whether the list is showing exactly @p expected, once it has settled.
        ///
        /// A filter change is a row folding shut over a motion token rather than a switch, so
        /// a row on its way out is still drawn for the length of the animation. Polling gives
        /// the fold the time it takes and no more; a fixed wait would have to be as long as
        /// the slowest token and would still be a guess.
        function settleOn(popup, expected) {
            const wanted = expected.slice().sort().join();
            return tryVerify(function () { return wordsShown(popup).sort().join() === wanted; },
                             1000, "the list is not showing " + wanted);
        }

        /// A delegate nested inside a Repeater inside a ListView, read through the model's own
        /// verdict rather than through the translated label the row draws.
        function test_theVerdictFilterNarrowsTheList() {
            const popup = make(wordsComponent);

            popup.filter = 1;
            const known = Controller.words.filter(function (e) { return e.verdict === "known"; }).map(function (e) { return e.word; });
            settleOn(popup, known);

            popup.filter = 2;
            const fresh = Controller.words.filter(function (e) { return e.verdict === "new"; }).map(function (e) { return e.word; });
            settleOn(popup, fresh);
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

        /// @return The window the question is drawn in. It is a window of its own rather than a
        ///         card inside the panel, so a case reaches it through an item that lands in it.
        function questionWindow(popup) {
            const heading = Util.textWith(Util.textsUnder(popup), [qsTr("Remove word")]);
            verify(heading, "the question is not drawn");
            return heading.Window.window;
        }

        /// The question, for the eye rather than for an assertion: a card is meant to be looked at
        /// before it is believed, and a word long enough to outgrow the card's floor is what shows
        /// whether the sentence still fits the card it is asked on.
        function test_captureTheRemovalQuestion() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the snapshot");

            // Every surface is a card that fades and lifts into place, so a grab taken before
            // that has finished photographs a half-transparent card.
            const settled = Tokens.motion.pop + 80;
            const was = Tokens.theme;

            const popup = make(wordsComponent);
            popup.visible = true;
            wait(settled);

            Tokens.theme = "light";
            wait(settled);
            verify(Util.saveSnapshot(testCase, popup.contentItem, lensQaSnapshotDir, "words-list-light"));

            popup.requestRemoval("counterrevolutionary");
            wait(settled);
            verify(Util.saveSnapshot(testCase, questionWindow(popup).contentItem,
                                     lensQaSnapshotDir, "words-removal-light"));

            Tokens.theme = "dark";
            wait(settled);
            verify(Util.saveSnapshot(testCase, questionWindow(popup).contentItem,
                                     lensQaSnapshotDir, "words-removal-dark"));

            popup.closeChild();
            popup.visible = false;
            Tokens.theme = was;
        }

        /// The cross sits on the row's own line, beside the two clusters that share it. Left to
        /// the Row it is laid in it takes the row's first pixel instead and reads high -- which is
        /// exactly what it did, since a row's line is taller than the 14 px glyph.
        function test_theRowCentresItsCrossOnItsLine() {
            const popup = make(wordsComponent);
            popup.visible = true;

            const word = "run";
            const row = rowFor(popup, word);
            verify(row, "the row for " + word + " is not on screen");

            const cross = crossFor(row);
            verify(cross, "the row draws no delete glyph");
            const pill = pillFor(row, "Known");

            const crossMid = cross.mapToItem(row, 0, cross.height / 2).y;
            const pillMid = pill.mapToItem(row, 0, pill.height / 2).y;
            verify(Math.abs(crossMid - pillMid) <= 0.5,
                   "the cross is " + (crossMid - pillMid).toFixed(1)
                   + " px off the line its verdict pills are on");
        }

        /// @return The delete glyph at the end of @p row, or null when the row draws none.
        function crossFor(row) {
            return Util.findAll(row, function (o) {
                return typeof o.source === "string" && o.source.indexOf("ui-close") >= 0;
            })[0];
        }

        /// @return The pill carrying @p label on the confirmation face.
        function pillWith(popup, label) {
            const text = Util.textWith(Util.textsUnder(popup), [label]);
            verify(text, "the confirmation draws no " + label + " pill");
            return text.parent;
        }

        /// @brief Press the delete glyph on @p word's row, whichever answer that turns out to be.
        function pressCross(popup, word) {
            const row = rowFor(popup, word);
            verify(row, "the row for " + word + " is not on screen");
            const cross = crossFor(row);
            verify(cross, "the row draws no delete glyph");
            mouseClick(cross, cross.width / 2, cross.height / 2);
        }

        /// @brief Open the question on @p word through the row's own cross.
        function askAbout(popup, word) {
            pressCross(popup, word);
            compare(popup.pendingRemoval, word, "the cross did not name the word it belongs to");
        }

        /// The confirmation the cross opens, and the answer that carries it out.
        ///
        /// The controller call was never in doubt, and nothing exercised it: that is how a native
        /// message box went on being the one surface in this product drawn by Windows. What is
        /// checked here is the path through the card instead -- the cross names the word, the
        /// question is drawn for that word, the answer reaches Controller.removeWord, and the
        /// list follows.
        function test_theDeleteCrossAsksBeforeTheWordGoes() {
            const popup = make(wordsComponent);
            popup.visible = true;

            // A word no other case in this file needs: this one is really removed, and the
            // Controller is shared by every case in the run.
            const word = "ubiquitous";
            verify(Controller.words.some(function (e) { return e.word === word; }),
                   "the fixture no longer carries " + word);

            askAbout(popup, word);

            const question = Util.textWith(Util.textsUnder(popup),
                                           [qsTr("Remove %1 from your word list and history?").arg(word)]);
            verify(question, "the card is not asking about " + word);
            verify(question.parent.visible, "the question is on a face that is not drawn");

            mouseClick(pillWith(popup, "Remove"), 5, 5);

            compare(popup.pendingRemoval, "", "the card stayed on the question after its answer");
            compare(Controller.words.some(function (e) { return e.word === word; }), false,
                    "the answer did not reach Controller.removeWord");
            compare(rowFor(popup, word), null, "the list still draws a word that is gone");
        }

        /// One removal a run is asked about, and the question opens with "do not ask again"
        /// already on -- so the run's second deletion is a deletion, not a question. This is the
        /// behaviour that was wrong on the real machine: the switch started off, and the reader
        /// who did not touch it was asked on every removal.
        function test_theAskedQuestionSilencesItselfByDefault() {
            const popup = make(wordsComponent);
            popup.visible = true;
            verify(popup.askBeforeRemoving, "the panel should start out asking");

            // Words no other case in this file needs: both are really removed.
            const answered = "ostensibly";
            const straightThrough = "juxtapose";

            askAbout(popup, answered);
            const question = questionWindow(popup);
            compare(question.visible, true, "the question did not open");

            const switcher = Util.ofType(popup, "Switch");
            verify(switcher, "the question carries no switch");
            compare(switcher.checked, true, "the question should be put with the switch already on");

            mouseClick(pillWith(popup, "Remove"), 5, 5);

            compare(popup.askBeforeRemoving, false, "the answer did not settle the asking");
            compare(Controller.words.some(function (e) { return e.word === answered; }), false,
                    "the answer did not reach Controller.removeWord");
            compare(question.visible, false, "the question stayed up");

            pressCross(popup, straightThrough);

            compare(popup.pendingRemoval, "", "a cross asked the question it was told not to ask");
            compare(question.visible, false, "the question opened with the asking switched off");
            compare(Controller.words.some(function (e) { return e.word === straightThrough; }), false,
                    "the silenced cross did not delete the word");
        }

        /// And the other answer the reader can give there: turn the switch off and answering the
        /// question settles nothing about asking, so the next removal is asked about again.
        function test_aQuestionThatIsLeftAskingComesBack() {
            const popup = make(wordsComponent);
            popup.visible = true;

            const answered = "ephemeral";
            const spared = "quintessential";

            askAbout(popup, answered);
            const switcher = Util.ofType(popup, "Switch");
            verify(switcher, "the question carries no switch");
            mouseClick(switcher, switcher.width / 2, switcher.height / 2);

            mouseClick(pillWith(popup, "Remove"), 5, 5);

            compare(popup.askBeforeRemoving, true, "the switch was off, so asking should stand");
            compare(Controller.words.some(function (e) { return e.word === answered; }), false,
                    "the answer did not reach Controller.removeWord");

            askAbout(popup, spared);
            const question = questionWindow(popup);
            compare(question.visible, true, "the question did not come back");
            compare(popup.askBeforeRemoving, true, "asking should stand, the switch having been off");
            // The switch stays where the reader left it rather than being reset per question: the
            // component writes its own `checked` when tapped, so a reset from elsewhere would have
            // it showing one thing and the decision reading another.
            compare(Util.ofType(popup, "Switch").checked, false,
                    "the switch should still be off, where the reader left it");

            mouseClick(pillWith(popup, "Cancel"), 5, 5);
            compare(question.visible, false, "Cancel did not put the question down");
            verify(Controller.words.some(function (e) { return e.word === spared; }),
                   "putting the question down removed the word");
        }

        /// The question is modal: the list behind it is not an answer to it, so a press that
        /// reaches it must not mark or delete anything. Measured rather than assumed, because the
        /// platform the suite runs on does not enforce modality -- this is the guard that does.
        function test_theListBehindTheQuestionTakesNoPress() {
            const popup = make(wordsComponent);
            popup.visible = true;

            const word = "run";
            const before = Controller.words.filter(function (e) { return e.word === word; })[0].verdict;

            askAbout(popup, word);
            const question = questionWindow(popup);

            const row = rowFor(popup, word);
            verify(row, "the row for " + word + " is not on screen");
            const pill = pillFor(row, "Known");
            mouseClick(pill, pill.width / 2, pill.height / 2);

            compare(question.visible, true, "the press behind the question dismissed it");
            compare(popup.pendingRemoval, word, "the press behind the question changed its subject");
            compare(Controller.words.filter(function (e) { return e.word === word; })[0].verdict,
                    before, "the list behind the question took a press");

            popup.closeChild();
        }

        /// The other two ways out, both of which have to leave the word alone: the reader who
        /// opened the question by mistake has to be able to put it down.
        function test_theConfirmationCanBePutDownWithoutRemovingAnything() {
            const popup = make(wordsComponent);
            popup.visible = true;
            const word = "counterrevolutionary";

            askAbout(popup, word);
            mouseClick(pillWith(popup, "Cancel"), 5, 5);

            compare(popup.pendingRemoval, "", "the card stayed on the question");
            verify(Controller.words.some(function (e) { return e.word === word; }),
                   "putting the question down removed the word");

            askAbout(popup, word);
            // keyClick takes no item. It lands on whatever window has the focus, which under the
            // offscreen platform is never the panel -- the reason the shortcut is
            // application-wide rather than window-scoped, and the reason this still reaches it.
            keyClick(Qt.Key_Escape);

            compare(popup.pendingRemoval, "", "Escape did not put the question down");
            verify(Controller.words.some(function (e) { return e.word === word; }),
                   "Escape removed the word");
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
