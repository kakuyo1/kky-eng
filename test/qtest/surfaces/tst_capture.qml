import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

Item {
    id: root
    width: 380
    height: 720

    Component {
        id: pageComponent
        SettingsCapturePage { x: 26; y: 20; width: 328; height: implicitHeight }
    }

    Component {
        id: pathFieldComponent
        PathField { width: 328 }
    }

    TestCase {
        name: "CaptureSettings"
        when: windowShown

        function make() {
            const page = createTemporaryObject(pageComponent, root);
            verify(!!page, "Capture page was created");
            return page;
        }

        function child(page, name) {
            const control = findChild(page, name);
            verify(!!control, "Object exists: " + name);
            return control;
        }

        function cleanup() {
            Controller.restoreCaptureDefaults();
            Controller.setTheme("dark");
            Tokens.theme = "dark";
        }

        function test_dragTiersFollowTheControllerFacade() {
            const page = make();
            const segment = child(page, "dragSensitivity");
            mouseClick(segment, 20, segment.height / 2);
            tryVerify(() => Controller.settings.dragSensitivity === "sensitive");
            mouseClick(segment, segment.width - 20, segment.height / 2);
            tryVerify(() => Controller.settings.dragSensitivity === "reluctant");
        }

        function test_minimumLengthWritesTheSelectedBoundary() {
            const page = make();
            const field = child(page, "minimumWordLength");
            mouseClick(field, field.width / 2, field.height / 2);
            tryVerify(() => field.openChildRect !== null);
            const row = Util.textWith(Util.textsInNestedWindow(field), ["5"]);
            verify(!!row, "The five-letter option exists");
            mouseClick(row.parent, row.parent.width / 2, row.parent.height / 2);
            tryVerify(() => Controller.settings.minimumWordLength === 5);
        }

        function test_whitelistEditingNormalizesAndRejectsInvalidInput() {
            const page = make();
            const field = child(page, "scanWhitelist");
            field.forceActiveFocus();
            field.text = "CHROME.EXE, firefox.exe";
            root.forceActiveFocus();
            tryVerify(() => Controller.settings.scanWhitelist === "chrome.exe;firefox.exe");
            field.forceActiveFocus();
            field.text = "../invalid.exe";
            root.forceActiveFocus();
            tryCompare(field, "text", "chrome.exe;firefox.exe");
        }

        /// The two paths are the reader's own Tesseract: they persist through the controller and
        /// the engine is rebuilt around them, which the availability the switches read follows.
        ///
        /// The picker itself is the running system's dialog -- FileDialog and FolderDialog are
        /// native windows a case cannot accept -- so what is checked here is the half that is
        /// this side of it: the path the controller is given is the path the field then shows,
        /// and an unchosen field shows what it would resolve to instead.
        function test_tesseractPathsReachTheController() {
            const page = make();
            const executable = child(page, "tesseractExecutable");
            const data = child(page, "tesseractDataDirectory");

            // Nothing chosen yet: both draw what OCR would use, in the placeholder colour.
            compare(executable.path, "", "an unchanged setting should start out unchosen");
            compare(shownPath(executable), Controller.settings.resolvedTesseractExecutable);
            compare(shownPath(data), Controller.settings.resolvedTesseractDataDirectory);
            compare(shownColour(executable), Tokens.faint, "an unchosen path is a placeholder");

            // What the dialog hands over is a url; what the setting holds is a local path. The
            // separator is the platform's, so this checks the conversion happened rather than
            // spelling out a path shape.
            Controller.setTesseractExecutable(Qt.url("file:///no-such-directory/tesseract.exe"));
            Controller.setTesseractDataDirectory(Qt.url("file:///no-such-directory/tessdata"));
            tryVerify(() => Controller.settings.tesseractExecutable.endsWith("tesseract.exe")
                             && Controller.settings.tesseractExecutable.indexOf("file:") < 0);
            tryVerify(() => Controller.settings.tesseractDataDirectory.endsWith("tessdata")
                             && Controller.settings.tesseractDataDirectory.indexOf("file:") < 0);

            compare(shownPath(executable), Controller.settings.tesseractExecutable);
            compare(shownColour(executable), Tokens.text, "a chosen path is not a placeholder");

            // Nothing is at either path, so the probe that follows the change cannot succeed and
            // the switches say so rather than offering a runtime that is not there.
            tryVerify(() => Controller.settings.ocrStatus !== "checking", 15000);
            compare(Controller.settings.ocrAvailable, false);
        }

        /// An unchosen field is not blank: it draws where the program would look instead, in the
        /// placeholder colour, so the reader sees the default before replacing it. Driven with a
        /// fallback this case owns rather than the machine's -- on a machine with no bundled
        /// Tesseract both sides of that comparison are empty and it would prove nothing.
        function test_thePathFieldShowsWhereItWouldLook() {
            const field = createTemporaryObject(pathFieldComponent, root);
            verify(!!field, "Path field was created");
            field.fallback = "app/ocr/tesseract.exe";

            compare(shownPath(field), field.fallback, "an unchosen field should draw the default");
            compare(shownColour(field), Tokens.faint, "a path nobody picked is a placeholder");

            field.path = "mine/tesseract.exe";
            compare(shownPath(field), field.path, "a chosen field should draw the choice");
            compare(shownColour(field), Tokens.text, "a picked path is not a placeholder");
        }

        /// @return The path @p field draws, chosen or resolved.
        function shownPath(field) {
            const text = Util.findAll(field, function (o) { return typeof o.text === "string"; })[0];
            verify(text, "the field draws no path");
            return text.text;
        }

        /// @return The colour that path is drawn in.
        function shownColour(field) {
            const text = Util.findAll(field, function (o) { return typeof o.text === "string"; })[0];
            verify(text, "the field draws no path");
            return text.color;
        }

        function test_ocrControlsUseControllerAndAvailability() {
            const page = make();
            tryVerify(() => Controller.settings.ocrStatus !== "checking", 15000);
            compare(child(page, "ocrCapture").interactive, Controller.settings.ocrAvailable);
            compare(child(page, "autoScan").interactive, Controller.settings.ocrAvailable);
        }
    }
}
