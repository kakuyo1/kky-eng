import QtQuick
import QtTest
import Lens

/**
 * The motion table: the durations UI.md's motion table names, and what reduced motion does to
 * them.
 *
 * The two tables below are built with an answer of their own rather than read off the machine,
 * so this case says the same thing whether or not Windows' animations are on. What it pins
 * down is that the durations go to zero together: one that is missed is one animation that
 * still runs for a reader who asked for none, and nothing else would catch it -- a duration is
 * a number no surface is required to be consistent with another about.
 */
Item {
    id: root
    width: 200
    height: 200

    Component {
        id: stillComponent
        Motion { reduced: true }
    }

    Component {
        id: movingComponent
        Motion { reduced: false }
    }

    TestCase {
        name: "Motion"
        when: windowShown

        function make(component) {
            const table = createTemporaryObject(component, root);
            verify(table);
            return table;
        }

        function test_everyDurationCollapsesWhenMotionIsReduced() {
            const still = make(stillComponent);

            compare(still.press, 0);
            compare(still.pop, 0);
            compare(still.bubble, 0);
        }

        function test_theDurationsAreTheOnesTheSpecNames() {
            const moving = make(movingComponent);

            compare(moving.press, 150);
            compare(moving.pop, 180);
            compare(moving.bubble, 220);
        }

        /// The curve is not a duration: reduced motion takes the movement out of an animation
        /// by making it instant, and a curve with nothing to ride changes nothing.
        function test_reducedMotionLeavesTheCurveAlone() {
            compare(make(stillComponent).easing, make(movingComponent).easing);
        }

        /// The surfaces read Tokens.motion.<token> and never a table of their own, so a token
        /// that stopped being forwarded would read as undefined and every animation built on it
        /// would quietly be skipped.
        function test_tokensForwardsTheTable() {
            compare(typeof Tokens.motion.press, "number");
            compare(typeof Tokens.motion.pop, "number");
            compare(typeof Tokens.motion.bubble, "number");
            compare(typeof Tokens.motion.easing, "number");
        }
    }
}
