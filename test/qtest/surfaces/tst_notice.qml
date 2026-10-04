import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The notice surface for failed requests.
 *
 * The controller owns the payload; this case exercises the surface contract with the failure
 * families that can actually reach it. Every current path is an error: network, key, and schema
 * failures. A failed selection grab is dropped silently (a popup per miss was a nag), and the
 * three channels mean a selection always has somewhere to go, so the old "no available channel"
 * info notice no longer exists. The daily budget event is a phase-two placeholder and has no
 * production payload or fixture here.
 */
Item {
    id: root
    width: 520
    height: 420

    Component {
        id: noticeComponent
        Notice {}
    }

    Component {
        id: bubbleComponent
        Bubble {}
    }

    TestCase {
        id: testCase
        name: "Notice"
        when: windowShown

        function opened(title, body, kind) {
            const notice = createTemporaryObject(noticeComponent, root);
            verify(!!notice, "Component exists");
            notice.show({title: title, body: body, kind: kind});
            wait(50);
            return notice;
        }

        function test_reachableFailureFamiliesRenderTheNoticeContract() {
            const failures = [
                { title: "Network", body: "The request could not reach the service.", kind: "error" },
                { title: "Schema", body: "The explanation response was invalid.", kind: "error" },
                { title: "API key", body: "The API key is missing.", kind: "error" }
            ];

            for (let i = 0; i < failures.length; ++i) {
                const notice = opened(failures[i].title, failures[i].body, failures[i].kind);
                compare(notice.noticeTitle, failures[i].title);
                compare(notice.noticeBody, failures[i].body);
                compare(notice.noticeKind, failures[i].kind);
                compare(notice.visible, true);
                notice.visible = false;
                notice.destroy();
            }
        }

        function test_closeDoesNotDismissAnotherSurface() {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(!!bubble, "Component exists");
            bubble.show({title: "profile", type: "word", ipa: "", en: "profile", zh: "", status: "new", x: 220, y: 260});

            const notice = opened("Network", "The request failed.", "error");
            notice.closeNotice();
            compare(notice.visible, false);
            compare(bubble.visible, true);

            bubble.visible = false;
        }

        function test_theClosePathHidesTheWindow() {
            const notice = opened("Network", "The request could not reach the service.", "error");
            notice.closeNotice();
            tryCompare(notice, "visible", false);
        }

        function test_captureTheNotice() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qml-snapshot.ps1) to save the snapshot");

            // Every surface is a card that fades and lifts into place (ShadowCard.qml), so a
            // grab taken before that has finished photographs a half-transparent card. Waiting
            // out the token is what makes the still show what the reader ends up looking at.
            const settled = Tokens.motion.pop + 80;

            Tokens.theme = "light";
            const notice = opened("API key", "The API key is missing.", "error");
            wait(settled);
            verify(Util.saveSnapshot(testCase, notice.contentItem, lensQaSnapshotDir, "notice-light"));
            notice.visible = false;

            Tokens.theme = "dark";
            notice.show({title: "Network", body: "The request could not reach the service.", kind: "error"});
            wait(settled);
            verify(Util.saveSnapshot(testCase, notice.contentItem, lensQaSnapshotDir, "notice-dark"));
            notice.visible = false;
        }
    }
}
