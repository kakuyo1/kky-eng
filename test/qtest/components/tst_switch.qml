import QtQuick
import QtTest
import Lens

/**
 * The 34x20 pill switch.
 *
 * A non-interactive switch is the case with a rule behind it: the phase-1 placeholders have to
 * read as unavailable without saying why. So it is dimmed and it answers nothing, and the
 * assertion is on both halves -- the dimming alone would leave a switch that still toggles.
 */
Item {
    id: root
    width: 200
    height: 120

    Component {
        id: switchComponent
        Switch {}
    }

    TestCase {
        name: "Switch"
        when: windowShown

        function make(properties) {
            const switcher = createTemporaryObject(switchComponent, root, properties);
            verify(switcher);
            return switcher;
        }

        function test_clickingFlipsItAndReportsTheNewState() {
            const switcher = make({});
            let reported = null;
            switcher.toggled.connect(function (on) { reported = on; });

            compare(switcher.checked, false);
            mouseClick(switcher, switcher.width / 2, switcher.height / 2);

            compare(switcher.checked, true);
            compare(reported, true);
        }

        function test_aNonInteractiveSwitchIsDimmedAndAnswersNothing() {
            const switcher = make({ interactive: false });
            let reported = false;
            switcher.toggled.connect(function () { reported = true; });

            verify(switcher.opacity < 1.0, "a disabled switch should be dimmed");
            mouseClick(switcher, switcher.width / 2, switcher.height / 2);

            compare(switcher.checked, false, "a disabled switch should not toggle");
            compare(reported, false);
        }
    }
}
