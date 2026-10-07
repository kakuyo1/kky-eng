import QtQuick

Item {
    id: root
    property var settings: Controller.settings

    signal childRequested(string route)

    /// Two dropdowns can be open, and only one at a time: whichever opened last is the one that
    /// took the press. Both are closed by the same call, and the panel asks for whichever is down.
    readonly property var openChildRect: providerField.openChildRect || modelField.openChildRect
    implicitHeight: content.implicitHeight

    function closeChild() {
        providerField.closeList();
        modelField.closeList();
    }

    /// The models this provider carries: what the service answered last time, else the catalog's
    /// own seed, else nothing at all. Its own controller property rather than a key of `settings`
    /// -- see ExplanationDuty::models(). Taken as a property, not read off the singleton, so a
    /// case can hand the page a list of its own.
    property var modelOptions: Controller.models || []

    Column {
        id: content
        width: parent.width
        spacing: 16

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Provider")
                color: Tokens.muted
                font.pixelSize: 12
            }
            DropdownField {
                id: providerField
                objectName: "providerField"
                width: parent.width
                options: root.settings.providers || []
                currentValue: root.settings.provider
                onPicked: (value) => Controller.setProvider(value)
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Model")
                color: Tokens.muted
                font.pixelSize: 12
            }

            /// One field, always the same shape: the value is the reader's to type, and the
            /// chevron opens the list the service gave -- which is a convenience, not the whole
            /// answer. A model the service would never list (a new one, a private deployment) has
            /// to stay reachable, and a custom endpoint names no models at all; a field that
            /// changed shape between the two read as a setting that had not taken (docs/adr/0017).
            ///
            DropdownField {
                id: modelField
                objectName: "modelField"
                width: parent.width
                editable: true
                options: root.modelOptions
                currentValue: root.settings.model
                onPicked: (value) => Controller.setModel(value)
                onEdited: (text) => Controller.setModel(text)
            }

            Text {
                width: parent.width
                text: root.settings.modelPrice || ""
                color: Tokens.muted
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
        }

        SettingsLinkRow {
            label: qsTranslate("SettingsPopup", "API configuration")
            note: root.settings.hasApiKey
                   ? qsTranslate("SettingsPopup", "Configured")
                   : qsTranslate("SettingsPopup", "Not configured")
            onPicked: root.childRequested("connection")
        }
    }
}
