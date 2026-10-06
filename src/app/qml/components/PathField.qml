pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs

/**
 * One path the reader picks rather than types.
 *
 * It keeps an input's shape -- same fill, border and radius -- and puts the folder glyph a picker
 * is read by at its right end. The whole field is the target, because a reader who clicks a path
 * expects the system's dialog, not a cursor in the middle of it: the glyph is what says so before
 * they try, and typing a path is what this replaced.
 *
 * A path nobody has picked yet draws what it would resolve to in the faint colour a placeholder
 * uses, rather than the empty field an unchosen setting otherwise is. The reader can then see
 * where the program will look before deciding to move it.
 *
 * The path is elided in the middle: a path is read by its two ends, and the file or folder name at
 * the far end is what tells the reader which one they are looking at.
 */
Item {
    id: root

    /// The path the reader picked, "" when they have not picked one.
    property string path: ""
    /// What "" resolves to. Drawn in the placeholder colour while there is no picked path.
    property string fallback: ""
    /// Whether this one picks a directory rather than a file.
    property bool folder: false
    /// The field's name: its dialog's title, and its name to assistive tools.
    property string label: ""

    /// A path was picked. Carried as a url, since that is what the dialog answers with; the
    /// controller turns it into a local path where the rest of the C++ already takes one.
    signal chosen(url path)

    implicitHeight: 35

    Rectangle {
        anchors.fill: parent
        radius: Tokens.radiusField
        color: Tokens.panel2
        border.width: 1
        border.color: Tokens.line

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 11
            anchors.right: glyph.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.path.length > 0 ? root.path : root.fallback
            color: root.path.length > 0 ? Tokens.text : Tokens.faint
            font.pixelSize: 12
            elide: Text.ElideMiddle
        }

        Icon {
            id: glyph
            anchors.right: parent.right
            anchors.rightMargin: 11
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/icons/ui-folder.svg"
            color: Tokens.faint
        }

        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.folder ? folderPicker.open() : filePicker.open() }
    }

    // Two dialogs rather than one with a mode: Qt 6 gave a folder its own type, and FileDialog's
    // modes are all about files. Only the one the field picks is ever opened.
    FileDialog {
        id: filePicker
        title: root.label
        onAccepted: root.chosen(filePicker.selectedFile)
    }

    FolderDialog {
        id: folderPicker
        title: root.label
        onAccepted: root.chosen(folderPicker.selectedFolder)
    }
}
