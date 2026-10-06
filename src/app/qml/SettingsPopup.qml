pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The settings surface from UI.md section 4.4 and ui-prototypes/settings.html.
 *
 * The window is fixed-size. Navigation changes the loaded page inside the card rather than
 * changing the surface geometry, so placement beside the tray remains stable.
 */
Window {
    id: settings

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 380
    readonly property int cardHeight: 480
    property string category: ""
    property string route: ""
    property string apiDraft: ""
    property string saveStatus: ""
    readonly property bool hasUnsavedChanges: apiDraft.length > 0
    // qmllint disable missing-property
    readonly property var openChildRect: viewLoader.status === Loader.Ready && viewLoader.item
        ? viewLoader.item.openChildRect
        : null
    // qmllint enable missing-property

    width: cardWidth + 2 * shadowMargin
    height: cardHeight + 2 * shadowMargin

    function categoryTitle(value) {
        if (value === "general") return qsTranslate("SettingsPopup", "General");
        if (value === "learning") return qsTranslate("SettingsPopup", "Reading & learning");
        if (value === "capture") return qsTranslate("SettingsPopup", "Capture & popups");
        if (value === "model") return qsTranslate("SettingsPopup", "Model service");
        if (value === "budget") return qsTranslate("SettingsPopup", "Daily budget");
        if (value === "extensions") return qsTranslate("SettingsPopup", "Extensions");
        return "";
    }

    function routeTitle(value) {
        if (value === "clipboard") return qsTranslate("SettingsPopup", "Popup & clipboard");
        if (value === "connection") return qsTranslate("SettingsPopup", "API configuration");
        return categoryTitle(category);
    }

    function closeChild() {
        // qmllint disable missing-property
        if (viewLoader.status === Loader.Ready && viewLoader.item && viewLoader.item.closeChild)
            viewLoader.item.closeChild();
        // qmllint enable missing-property
    }

    function openCategory(value) {
        closeChild();
        category = value;
        route = "";
        body.contentY = 0;
    }

    function openRoute(value) {
        closeChild();
        route = value;
        body.contentY = 0;
    }

    function navigateBack() {
        if (route !== "") {
            closeChild();
            route = "";
        } else if (category !== "") {
            category = "";
        } else {
            visible = false;
        }
        body.contentY = 0;
    }

    function saveSettings() {
        if (apiDraft.length > 0)
            Controller.setApiKey(apiDraft);
        apiDraft = "";
        saveStatus = qsTranslate("SettingsPopup", "Saved");
    }

    function restoreDefaults() {
        Controller.setLevel(2);
        Controller.setExplanationLang("en");
        Controller.setMultiSense(false);
        Controller.setTheme("light");
        Controller.setUiLanguage("zh");
        Controller.setSelectionCapture(true);
        Controller.setClipboardPolicy("topmost");
        Controller.setPopupFrequency("standard");
        Controller.setProvider("DeepSeek");
        Controller.setModel("deepseek-flash");
        Controller.restoreCaptureDefaults();
        saveStatus = qsTranslate("SettingsPopup", "Defaults restored");
    }

    onVisibleChanged: {
        if (visible) {
            category = "";
            route = "";
            apiDraft = "";
            saveStatus = "";
            body.contentY = 0;
        } else {
            closeChild();
        }
    }

    Shortcut {
        sequence: "Escape"
        onActivated: settings.navigateBack()
    }

    ShadowCard {
        anchors.fill: parent
        radius: Tokens.radiusCard
        movable: true
        onDraggingChanged: if (dragging) settings.closeChild()

        Item {
            id: header
            x: 0
            y: 0
            width: settings.cardWidth
            height: 58

            Item {
                visible: settings.category !== ""
                x: 14
                y: 16
                width: 26
                height: 26

                Icon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    source: "qrc:/icons/ui-back.svg"
                    color: Tokens.muted
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: settings.navigateBack() }
            }

            Text {
                x: settings.category !== "" ? 54 : 20
                anchors.verticalCenter: parent.verticalCenter
                text: qsTranslate("SettingsPopup", "Settings")
                color: Tokens.text
                font.pixelSize: 14
                font.weight: Font.Bold
            }

            Text {
                visible: settings.category !== ""
                anchors.left: parent.left
                anchors.leftMargin: 120
                anchors.verticalCenter: parent.verticalCenter
                text: "/"
                color: Tokens.faint
                font.pixelSize: 12
            }

            Text {
                visible: settings.category !== ""
                anchors.left: parent.left
                anchors.leftMargin: 136
                anchors.right: closeButton.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: settings.route !== "" ? settings.routeTitle(settings.route) : settings.categoryTitle(settings.category)
                color: Tokens.muted
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            Item {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                width: 26
                height: 26

                Icon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    source: "qrc:/icons/ui-close.svg"
                    color: Tokens.faint
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: settings.visible = false }
            }
        }

        Flickable {
            id: body
            x: 0
            y: header.height
            width: settings.cardWidth
            height: settings.cardHeight - header.height - footer.height
            clip: true
            contentWidth: width
            contentHeight: Math.max(height, viewLoader.y + viewLoader.height + 18)

            Loader {
                id: viewLoader
                x: 20
                y: 12
                width: settings.cardWidth - 40
                // qmllint disable missing-property
                height: status === Loader.Ready && item ? item.implicitHeight : 0
                // qmllint enable missing-property
                sourceComponent: settings.route === "clipboard" ? clipboardComponent
                                 : settings.route === "connection" ? connectionComponent
                                 : settings.category === "" ? catalogComponent
                                  : settings.category === "general" ? generalComponent
                                  : settings.category === "learning" ? learningComponent
                                  : settings.category === "capture" ? captureComponent
                                  : settings.category === "model" ? modelComponent
                                  : settings.category === "budget" ? budgetComponent
                                  : extensionsComponent
            }
        }

        Item {
            id: footer
            x: 0
            y: settings.cardHeight - height
            width: settings.cardWidth
            height: 56

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Tokens.line2
            }

            Rectangle {
                x: 20
                y: 15
                width: defaultsLabel.width + 28
                height: 27
                radius: Tokens.radiusPill
                color: Tokens.panel2
                border.width: 1
                border.color: Tokens.line

                Text {
                    id: defaultsLabel
                    anchors.centerIn: parent
                    text: qsTranslate("SettingsPopup", "Defaults")
                    color: Tokens.muted
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: settings.restoreDefaults() }
            }

            Text {
                anchors.left: defaultsLabel.parent.right
                anchors.leftMargin: 8
                anchors.right: saveButton.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: settings.hasUnsavedChanges
                      ? qsTranslate("SettingsPopup", "Unsaved changes")
                      : settings.saveStatus
                color: Tokens.faint
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            Rectangle {
                id: saveButton
                anchors.right: parent.right
                anchors.rightMargin: 20
                y: 15
                width: saveLabel.width + 28
                height: 27
                radius: Tokens.radiusPill
                color: Tokens.ink
                opacity: settings.hasUnsavedChanges ? 1 : 0.45

                Text {
                    id: saveLabel
                    anchors.centerIn: parent
                    text: qsTranslate("SettingsPopup", "Save")
                    color: Tokens.on
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }
                HoverHandler {
                    cursorShape: settings.hasUnsavedChanges ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
                TapHandler {
                    enabled: settings.hasUnsavedChanges
                    onTapped: settings.saveSettings()
                }
            }
        }
    }

    Component {
        id: catalogComponent
        SettingsCatalog {
            onCategoryRequested: (value) => settings.openCategory(value)
        }
    }
    Component { id: generalComponent; SettingsGeneralPage {} }
    Component { id: learningComponent; SettingsLearningPage {} }
    Component {
        id: captureComponent
        SettingsCapturePage {
            onChildRequested: (value) => settings.openRoute(value)
        }
    }
    Component {
        id: modelComponent
        SettingsModelPage {
            onChildRequested: (value) => settings.openRoute(value)
        }
    }
    Component {
        id: budgetComponent
        SettingsBudgetPage {
            onBudgetChanged: (amount) => Controller.setDailyBudget(amount)
        }
    }
    Component { id: extensionsComponent; SettingsExtensionsPage {} }
    Component { id: clipboardComponent; SettingsClipboardPage {} }
    Component {
        id: connectionComponent
        SettingsConnectionPage {
            draftOwner: settings
            onApiEdited: (value) => settings.apiDraft = value
        }
    }
}
