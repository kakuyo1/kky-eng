import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * Pointing at a word in the list, which is how a word the reader explained once is read again
 * without spending anything.
 *
 * What is checked is the contract between the row and the controller, not the surfaces that draw
 * it: the row is reached through the words panel and the answer is read off `Controller.bubble` and
 * `Controller.notice`, the way the panels are read in tst_panels. Main.qml is what turns either of
 * those into a window, and it is not built here.
 *
 * Two rows are enough for both halves, and the fixture has them: `panel` has one stored explanation
 * in the language the fixture explains in (`test/qtest/surfaces/settings.json`), and every other
 * word in its history was explained in a run that stored nothing.
 *
 * Which word carries the stored one is not free. The suite shares one controller and its document,
 * and `tst_panels.qml` really deletes the words it is given -- a deletion drops that lemma's stored
 * explanations with it (Controller.removeWord). `panel` is a word that file never names, and any
 * replacement has to be one too.
 *
 * The dwell before an answer is 400 ms, so a case that wants one waits for it; a case that wants
 * the pointer to have moved on has to move it somewhere else, because a row that is never left
 * never reports leaving.
 */
Item {
    id: root
    width: 640
    height: 640

    Component {
        id: wordsComponent
        WordsPopup {}
    }

    TestCase {
        id: testCase
        name: "Review"
        when: windowShown

        /// The stored word, and one that has nothing stored.
        readonly property string stored: "panel"
        readonly property string unstored: "counterrevolutionary"

        /// Pin what the store is pointed at before any of this runs. The suite shares one
        /// controller, and the settings cases set the explanation language to whatever they are
        /// exercising -- a stored explanation is keyed by the language it was stored in, so a case
        /// that reads one has to say which language it is reading. Multi-sense changes the key too.
        function initTestCase() {
            Controller.setExplanationLang("zh");
            Controller.setMultiSense(false);
        }

        function make() {
            const popup = createTemporaryObject(wordsComponent, root);
            verify(popup);
            popup.visible = true;
            wait(50);
            return popup;
        }

        /// @return The row drawing @p word.
        function rowFor(popup, word) {
            const text = Util.textWith(Util.textsUnder(popup), [word]);
            verify(text, "the list is not showing " + word);
            return text.parent;
        }

        /// @brief Point at @p word and let the dwell run out. The pointer goes on the word itself
        ///        rather than on the row: the word is what the row watches for a reading, and a
        ///        row's middle is empty space beside a short word.
        function dwellOn(popup, word) {
            const drawn = Util.textWith(Util.textsUnder(popup), [word]);
            verify(drawn, "the list is not showing " + word);
            mouseMove(drawn, 2, drawn.height / 2);
            wait(500);
        }

        function cleanup() {
            Controller.dismissBubble();
            Controller.dismissNotice();
        }

        /// Hovering a row raises what that word is already stored as -- the whole point of the
        /// feature, and the reason no request is made: the copy is the one the reader read before.
        function test_hoveringAStoredWordRaisesItsStoredExplanation() {
            const popup = make();
            dwellOn(popup, stored);

            const bubble = Controller.bubble;
            compare(bubble.title, stored);
            compare(bubble.type, "word");
            compare(bubble.ipa, "/ˈpænl/");
            compare(bubble.senses.length, 1, "the stored entry has one sense");
            compare(bubble.senses[0].text, "面板；专门小组");
            compare(Object.keys(Controller.notice).length, 0, "a stored word leaves no notice behind");
        }

        /// A bubble raised by a pointer on a row belongs to that row: the pointer leaving is what
        /// takes it down, so the bubble carries no countdown of its own to disagree with.
        function test_theReadingGoesWhenThePointerLeavesTheRow() {
            const popup = make();
            dwellOn(popup, stored);
            compare(Controller.bubble.title, stored);

            mouseMove(popup, 2, 2);
            tryVerify(function () { return Object.keys(Controller.bubble).length === 0; }, 1000,
                      "the bubble outlived the pointer leaving its row");
        }

        /// A word with nothing stored in this language is said to be, rather than left looking like
        /// a hover that did not register -- and the way to get it explained is offered as a button
        /// the reader presses, not as a request their hover makes.
        function test_aWordWithNothingStoredOffersToAskTheModel() {
            const popup = make();
            dwellOn(popup, unstored);

            const notice = Controller.notice;
            compare(notice.title, unstored);
            compare(notice.kind, "info");
            compare(notice.lemma, unstored, "the button has to act on the word the row drew");
            compare(notice.action, qsTranslate("lens::app::AppController", "Explain now"));
            compare(Object.keys(Controller.bubble).length, 0, "nothing stored means no bubble");
        }

        /// The notice is a card, not a bubble: it is not the pointer's to take down, because the
        /// button on it is reached by moving the pointer onto it.
        function test_theOfferOutlivesThePointerLeavingTheRow() {
            const popup = make();
            dwellOn(popup, unstored);
            compare(Controller.notice.title, unstored);

            mouseMove(popup, 2, 2);
            wait(300);
            compare(Controller.notice.title, unstored, "the offer went with the pointer");
        }

        /// The button's action, at the controller: a word whose explanation is already stored is
        /// answered from the store. That is the half of this path a test can run -- the other half
        /// is a request, and nothing in this tree asks the model anything.
        function test_askingAboutAStoredWordAnswersFromTheStore() {
            Controller.explainLemma(stored);

            compare(Controller.bubble.title, stored);
            compare(Object.keys(Controller.notice).length, 0, "an answered word leaves no notice behind");
        }

        /// Hovering a stored word after an offer is up takes the offer down: one surface, and the
        /// newer question is the one being asked.
        function test_storedReadingClearsTheOffer() {
            const popup = make();
            dwellOn(popup, unstored);
            compare(Controller.notice.title, unstored);

            dwellOn(popup, stored);
            compare(Controller.bubble.title, stored);
            compare(Object.keys(Controller.notice).length, 0, "the offer stayed up behind the reading");
        }
    }
}
