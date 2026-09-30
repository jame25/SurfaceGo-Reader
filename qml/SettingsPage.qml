import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page
    signal back()

    background: Rectangle { color: Theme.background }

    header: ToolBar {
        height: Theme.touch + 8
        background: Rectangle { color: Theme.bar }
        RowLayout {
            anchors.fill: parent
            TouchButton { iconName: "back"; label: "Back"; onClicked: page.back() }
            Label {
                text: "Settings"
                font.pixelSize: 24
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }
        }
    }

    component SectionTitle: Label {
        Layout.fillWidth: true
        Layout.topMargin: 18
        font.pixelSize: 15
        font.bold: true
        font.capitalization: Font.AllUppercase
        color: Theme.accent
    }

    component SettingSwitch: SwitchDelegate {
        Layout.fillWidth: true
        implicitHeight: Theme.touch + 8
        font.pixelSize: 18
    }

    component FolderRow: ItemDelegate {
        id: row
        property string title
        property string path
        signal choose()
        Layout.fillWidth: true
        implicitHeight: Theme.touch + 20
        contentItem: RowLayout {
            spacing: 12
            TouchButton {
                iconName: "folder"
                icon.color: Theme.accent
                onClicked: row.choose()
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: row.title; font.pixelSize: 18; color: Theme.text }
                Label {
                    Layout.fillWidth: true
                    text: row.path
                    font.pixelSize: 14
                    color: Theme.subtext
                    elide: Text.ElideMiddle
                }
            }
            Label { text: "Change"; color: Theme.accent; font.pixelSize: 16 }
        }
        onClicked: choose()
    }

    component SettingLabel: Label {
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.topMargin: 10
        font.pixelSize: 18
        color: Theme.text
    }

    // Selectable sample tile: a small card showing the colours it stands for.
    component Swatch: AbstractButton {
        id: swatch
        property color fill
        property color ink
        property color chip: "transparent"
        property bool boldSample: false
        property bool selected: false
        implicitWidth: 104
        implicitHeight: 100
        padding: 0
        focusPolicy: Qt.NoFocus
        Accessible.name: text
        Accessible.checkable: true
        Accessible.checked: selected
        opacity: pressed ? 0.7 : 1.0
        contentItem: ColumnLayout {
            spacing: 6
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 88
                implicitHeight: 60
                radius: 10
                color: swatch.fill
                border.width: swatch.selected ? 3 : 1
                border.color: swatch.selected ? Theme.accent : Theme.divider
                Rectangle {
                    anchors.centerIn: parent
                    width: sample.implicitWidth + 12
                    height: sample.implicitHeight + 4
                    radius: 4
                    color: swatch.chip
                }
                Label {
                    id: sample
                    anchors.centerIn: parent
                    text: "Aa"
                    color: swatch.ink
                    font.family: "serif"
                    font.pixelSize: 22
                    font.bold: swatch.boldSample
                }
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: swatch.text
                font.pixelSize: 15
                font.bold: swatch.selected
                color: swatch.selected ? Theme.accent : Theme.subtext
            }
        }
        background: null
    }

    function capitalize(s) { return s.charAt(0).toUpperCase() + s.slice(1) }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 40
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar { width: 10 }

        ColumnLayout {
            id: column
            width: Math.min(parent.width - 2 * Theme.margin, 820)
            x: (parent.width - width) / 2
            y: 8
            spacing: 2

            SectionTitle { text: "Folders" }
            FolderRow {
                title: "Library folder"
                path: appSettings.libraryFolder
                onChoose: folderPicker.pick("Choose library folder", appSettings.libraryFolder,
                                            p => appSettings.libraryFolder = p)
            }
            FolderRow {
                title: "Piper voices folder"
                path: appSettings.voicesFolder
                onChoose: folderPicker.pick("Choose voices folder", appSettings.voicesFolder, p => {
                    appSettings.voicesFolder = p
                    reader.reloadVoice()
                })
            }

            SectionTitle { text: "Appearance" }
            SettingLabel { text: "Background" }
            Flow {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                spacing: 8
                Repeater {
                    model: Theme.themeNames
                    Swatch {
                        required property string modelData
                        text: page.capitalize(modelData)
                        fill: Theme.themes[modelData].background
                        ink: Theme.themes[modelData].text
                        selected: appSettings.theme === modelData
                        onClicked: appSettings.theme = modelData
                    }
                }
            }
            SettingLabel { text: "Sentence highlight" }
            Flow {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                spacing: 8
                Repeater {
                    model: Theme.highlightNames
                    Swatch {
                        required property string modelData
                        readonly property var pair: Theme.highlights[modelData][Theme.dark ? "dark" : "light"]
                        text: page.capitalize(modelData)
                        fill: Theme.background
                        chip: pair[0]
                        ink: pair[1]
                        selected: appSettings.highlightColor === modelData
                        onClicked: appSettings.highlightColor = modelData
                    }
                }
            }
            SettingLabel { text: "Word highlight" }
            Flow {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                spacing: 8
                Swatch {
                    text: "Bold"
                    fill: Theme.background
                    chip: Theme.sentence
                    ink: Theme.sentenceText
                    boldSample: true
                    selected: appSettings.wordHighlight === "bold"
                    onClicked: appSettings.wordHighlight = "bold"
                }
                Swatch {
                    text: "Inverted"
                    fill: Theme.background
                    chip: Theme.sentenceText
                    ink: Theme.sentence
                    selected: appSettings.wordHighlight === "inverted"
                    onClicked: appSettings.wordHighlight = "inverted"
                }
            }
            // Live preview of the chosen colours.
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 12
                implicitHeight: preview.implicitHeight + 32
                radius: 10
                color: Theme.background
                border.width: 1
                border.color: Theme.divider
                Label {
                    id: preview
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 16 }
                    wrapMode: Text.Wrap
                    textFormat: Text.RichText
                    font.family: "serif"
                    font.pixelSize: 20
                    color: Theme.text
                    text: {
                        const word = appSettings.wordHighlight === "inverted"
                            ? "<span style=\"background-color:" + Theme.sentenceText + ";color:" + Theme.sentence + "\">spoken</span>"
                            : "<b>spoken</b>"
                        return "The morning was quiet. <span style=\"background-color:" + Theme.sentence
                             + ";color:" + Theme.sentenceText + "\">Each sentence is highlighted as it is "
                             + word + " aloud.</span> Then the next one begins."
                    }
                }
            }

            SectionTitle { text: "Reading" }
            SettingSwitch {
                text: "Auto-scroll follows playback"
                checked: appSettings.autoScroll
                onToggled: appSettings.autoScroll = checked
            }
            SettingSwitch {
                text: "Word wrap"
                checked: appSettings.wordWrap
                onToggled: appSettings.wordWrap = checked
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touch + 8
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    text: "Text size"
                    font.pixelSize: 18
                    color: Theme.text
                }
                TouchButton { text: "A−"; onClicked: appSettings.fontSize = appSettings.fontSize - 2 }
                Label {
                    text: appSettings.fontSize + " px"
                    font.pixelSize: 18
                    color: Theme.text
                    horizontalAlignment: Text.AlignHCenter
                    Layout.preferredWidth: 70
                }
                TouchButton { text: "A+"; onClicked: appSettings.fontSize = appSettings.fontSize + 2 }
            }

            SectionTitle { text: "Speech" }
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touch + 8
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    spacing: 0
                    Label { text: "Highlight timing offset"; font.pixelSize: 18; color: Theme.text }
                    Label {
                        Layout.fillWidth: true
                        text: "Only needed with extra output latency (e.g. Bluetooth headphones). Positive = highlight later."
                        wrapMode: Text.Wrap
                        font.pixelSize: 14
                        color: Theme.subtext
                    }
                }
                TouchButton { text: "−"; onClicked: appSettings.latencyOffsetMs = appSettings.latencyOffsetMs - 25 }
                Label {
                    text: appSettings.latencyOffsetMs + " ms"
                    font.pixelSize: 18
                    color: Theme.text
                    horizontalAlignment: Text.AlignHCenter
                    Layout.preferredWidth: 80
                }
                TouchButton { text: "+"; onClicked: appSettings.latencyOffsetMs = appSettings.latencyOffsetMs + 25 }
            }

            SectionTitle { text: "About" }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                wrapMode: Text.Wrap
                font.pixelSize: 15
                color: Theme.subtext
                text: "SurfaceGo Reader " + Qt.application.version
                      + "\nPiper voices run in-process with ONNX Runtime; eSpeak NG provides phonemes."
                      + "\nTips: tap any word to read from its sentence · long-press a book to remove it"
                      + " · pinch to zoom text · F11 toggles full screen."
            }
        }
    }

    FolderPicker {
        id: folderPicker
    }
}
