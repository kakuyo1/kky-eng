import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The notice surface for unavailable channels and failed requests.
 *
 * The controller owns the payload; this case exercises the surface contract with the six
 * reachable failure families. The daily budget event is a phase-two placeholder and has no
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
                { title: "Selection", body: "No word to explain in this selection", kind: "info" },
                { title: "network", body: "The request could not reach the service.", kind: "error" },
                { title: "schema", body: "The explanation response was invalid.", kind: "error" },
                { title: "key", body: "The API key is missing.", kind: "error" },
                { title: "clipboard", body: "The clipboard is busy, so the selection was not read.", kind: "error" },
                { title: "terminal", body: "Selection capture is unavailable in terminal applications.", kind: "error" }
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
            bubble.show({word: "profile", ipa: "", en: "profile", zh: "", status: "new", x: 220, y: 260});

            const notice = opened("network", "The request failed.", "error");
            notice.closeNotice();
            compare(notice.visible, false);
            compare(bubble.visible, true);

            bubble.visible = false;
        }

        function test_theClosePathHidesTheWindow() {
            const notice = opened("Selection", "No word to explain in this selection", "info");
            notice.closeNotice();
            tryCompare(notice, "visible", false);
        }

        function test_captureTheNotice() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qml-snapshot.ps1) to save the snapshot");

            Tokens.theme = "light";
            const notice = opened("Selection", "No word to explain in this selection", "info");
            verify(Util.saveSnapshot(testCase, notice.contentItem, lensQaSnapshotDir, "notice-light"));
            notice.visible = false;

            Tokens.theme = "dark";
            notice.show({title: "Network", body: "The request could not reach the service.", kind: "error"});
            wait(50);
            verify(Util.saveSnapshot(testCase, notice.contentItem, lensQaSnapshotDir, "notice-dark"));
            notice.visible = false;
        }
    }
}
