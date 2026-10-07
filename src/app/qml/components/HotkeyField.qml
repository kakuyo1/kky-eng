pragma ComponentBehavior: Bound

import QtQuick

/**
 * The capture trigger key, as a row the reader presses the combination into (UI.md 4.4).
 *
 * The same lockup as SwitchRow -- a label and the value at opposite ends of one line -- because it
 * sits among those rows and answers the same kind of question. What the value is drawn with is the
 * one thing that differs: a combination is read key by key, so each part of it is a capsule the way
 * a keyboard's own key is lettered, and `Ctrl+Alt+S` reads as three keys rather than as a string.
 *
 * While it is recording, the row says what it is waiting for instead of showing the old
 * combination, and it takes the keyboard. The combination is not assembled here: the key and the
 * modifiers cross to the controller as they arrived, because the string a combination is shown as
 * belongs in one place and the controller is where it is decided.
 */
Item {
    id: root

    /// The combination in force, as the controller writes it: "Ctrl+Alt+S".
    property string combination: ""
    /// Whether the trigger can fire. False draws the line that says it cannot.
    property bool conflicted: false
    /// Whether the reader can change the combination. False greys the row and says nothing about
    /// why, the way every other switch this depends on behaves (UI.md 4.4).
    property bool interactive: true

    signal keyChosen(int key, int modifiers)

    /// Tall enough for the row, and for the reason underneath it when there is one.
    implicitHeight: box.height + (conflicted ? warning.height + 6 : 0)
    /// True while the row is waiting for a combination.
    readonly property bool recording: recorder.activeFocus

    /// Which keys are shown, one capsule each. An empty combination draws nothing.
    readonly property var parts: combination.length > 0 ? combination.split("+") : []

    Rectangle {
        id: box
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 35
        radius: Tokens.radiusField
        color: Tokens.panel2
        border.width: 1
        border.color: root.recording ? Tokens.ink : Tokens.line

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 11
            anchors.right: keys.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.recording ? qsTr("Press the combination") : qsTr("Trigger key")
            color: root.recording ? Tokens.muted : root.interactive ? Tokens.text : Tokens.faint
            font.pixelSize: 13
            elide: Text.ElideRight
        }

        Row {
            id: keys
            anchors.right: parent.right
            anchors.rightMargin: 9
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            visible: !root.recording

            Repeater {
                model: root.parts

                delegate: Rectangle {
                    id: capsule
                    required property string modelData

                    width: Math.max(18, keyText.implicitWidth + 12)
                    height: 19
                    radius: Tokens.radiusPill
                    color: Tokens.panel
                    border.width: 1
                    border.color: Tokens.line

                    Text {
                        id: keyText
                        anchors.centerIn: parent
                        text: capsule.modelData
                        color: Tokens.text
                        font.family: Tokens.monoFamily
                        font.pixelSize: 11
                    }
                }
            }
        }

        HoverHandler { cursorShape: root.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor }
        TapHandler {
            enabled: root.interactive
            onTapped: recorder.forceActiveFocus()
        }
    }

    /// The line under the row: the one thing that goes wrong with a trigger, which is that no key
    /// press of the reader's reaches it. Nothing is said about which, because there are only two
    /// ways to get here and the reader is the one who chose the combination.
    Text {
        id: warning
        anchors.top: box.bottom
        anchors.topMargin: 6
        width: parent.width
        visible: root.conflicted
        text: qsTr("Unavailable: it needs Ctrl or Alt, and no other program may hold it")
        color: Tokens.muted
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    /// Takes the combination. It has no shape of its own -- the box above is what the reader sees
    /// and clicks -- and exists to hold the focus that receives the keys.
    Item {
        id: recorder
        anchors.fill: box

        Keys.onPressed: (event) => {
            // Escape is the way out of a capture that is already going wrong, and a modifier on its
            // own is not a combination yet: the reader is on their way to the key that follows.
            if (event.key === Qt.Key_Escape) {
                recorder.focus = false;
                event.accepted = true;
                return;
            }
            if (event.key === Qt.Key_Control || event.key === Qt.Key_Alt || event.key === Qt.Key_Shift || event.key === Qt.Key_Meta) {
                event.accepted = true;
                return;
            }
            root.keyChosen(event.key, event.modifiers);
            recorder.focus = false;
            event.accepted = true;
        }
    }
}
