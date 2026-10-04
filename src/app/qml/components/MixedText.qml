pragma ComponentBehavior: Bound

import QtQuick

/**
 * A single-line value whose CJK glyphs use the UI face while Latin, digits and punctuation keep
 * the numeric face. This avoids letting one explicit family decide the fallback for the whole run.
 */
Item {
    id: root

    property string value: ""
    property int pixelSize: 13
    property int weight: Font.Normal
    property color color: "black"
    readonly property var runs: splitRuns(value)

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight
    width: implicitWidth
    height: implicitHeight

    function splitRuns(source) {
        const result = [];
        let current = "";
        let cjk = false;
        for (let i = 0; i < source.length; ++i) {
            const character = source.charAt(i);
            // Code units, not a regex: a regex literal inside a loop body makes qmllint
            // 6.9.0 consume memory until the machine dies, whatever the pattern says.
            const code = character.charCodeAt(0);
            const nextCjk = code >= 0x3400 && code <= 0x9fff;
            if (current !== "" && nextCjk !== cjk) {
                result.push({ text: current, cjk: cjk });
                current = "";
            }
            current += character;
            cjk = nextCjk;
        }
        if (current !== "")
            result.push({ text: current, cjk: cjk });
        return result;
    }

    Row {
        id: row
        spacing: 0

        Repeater {
            model: root.runs

            Text {
                required property var modelData
                text: modelData.text
                color: root.color
                font.pixelSize: root.pixelSize
                font.weight: root.weight
                font.family: modelData.cjk ? "" : Tokens.monoFamily
            }
        }
    }
}
