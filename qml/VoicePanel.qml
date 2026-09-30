import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Quick access to the Piper options: voice model, speaker and speed (length_scale).
Popup {
    id: panel
    parent: Overlay.overlay
    y: parent ? parent.height - height : 0
    modal: true
    dim: true
    padding: 20
    margins: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition { NumberAnimation { property: "y"; from: panel.parent.height; to: panel.parent.height - panel.height; duration: 180; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "y"; to: panel.parent.height; duration: 150 } }

    background: Rectangle {
        color: Theme.surface
        radius: 16
        Rectangle { // square bottom corners
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 16
            color: parent.color
        }
    }

    onOpened: {
        voiceList.refresh()
        speed.value = appSettings.lengthScale
    }

    contentItem: ColumnLayout {
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Voice"
                font.pixelSize: 22
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }
            TouchButton {
                iconName: "refresh"
                label: "Rescan voices"
                onClicked: reader.reloadVoice()
            }
        }

        ComboBox {
            id: voiceBox
            Layout.fillWidth: true
            implicitHeight: Theme.touch
            font.pixelSize: 18
            model: voiceList.voices
            textRole: "name"
            enabled: voiceList.count > 0
            displayText: voiceList.count > 0 ? currentText : "No voices found"
            currentIndex: voiceList.indexOfPath(appSettings.voice)
            delegate: ItemDelegate {
                required property int index
                required property var modelData
                width: voiceBox.width
                implicitHeight: Theme.touch
                font.pixelSize: 18
                text: modelData.name + (modelData.language ? "   (" + modelData.language + (modelData.quality ? ", " + modelData.quality : "") + ")" : "")
                highlighted: voiceBox.highlightedIndex === index
            }
            onActivated: index => appSettings.voice = voiceList.voices[index].path
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            wrapMode: Text.Wrap
            font.pixelSize: 15
            color: reader.voiceError.length ? "#e53935" : Theme.subtext
            text: reader.voiceError.length ? reader.voiceError
                : reader.voiceLoading ? "Loading voice model…"
                : voiceList.count === 0 ? "Put Piper voices (.onnx + .onnx.json) in " + appSettings.voicesFolder : ""
        }

        ComboBox {
            id: speakerBox
            Layout.fillWidth: true
            implicitHeight: Theme.touch
            visible: reader.speakers.length > 1
            font.pixelSize: 18
            model: reader.speakers
            currentIndex: Math.min(appSettings.speaker, reader.speakers.length - 1)
            onActivated: index => appSettings.speaker = index
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            Label {
                text: "Speed"
                font.pixelSize: 22
                font.bold: true
                color: Theme.text
            }
            Item { Layout.fillWidth: true }
            Label {
                text: (1 / speed.value).toFixed(2) + "×   ·   length_scale " + speed.value.toFixed(2)
                font.pixelSize: 16
                color: Theme.subtext
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            TouchButton {
                text: "Faster"
                font.pixelSize: 16
                onClicked: { speed.value = Math.max(speed.from, speed.value - 0.05); appSettings.lengthScale = speed.value }
            }
            Slider {
                id: speed
                Layout.fillWidth: true
                implicitHeight: Theme.touch
                // length_scale: lower = faster speech
                from: 0.5
                to: 2.0
                stepSize: 0.05
                snapMode: Slider.SnapAlways
                value: appSettings.lengthScale
                // Apply on release: changing speed re-synthesizes the upcoming audio.
                onPressedChanged: if (!pressed) appSettings.lengthScale = value
            }
            TouchButton {
                text: "Slower"
                font.pixelSize: 16
                onClicked: { speed.value = Math.min(speed.to, speed.value + 0.05); appSettings.lengthScale = speed.value }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: [0.7, 0.85, 1.0, 1.15, 1.3]
                delegate: PillButton {
                    required property var modelData
                    Layout.fillWidth: true
                    accent: Math.abs(appSettings.lengthScale - modelData) < 0.001
                    text: modelData.toFixed(2)
                    onClicked: { speed.value = modelData; appSettings.lengthScale = modelData }
                }
            }
        }

        Button {
            Layout.alignment: Qt.AlignRight
            text: "Done"
            implicitHeight: Theme.touch
            flat: true
            onClicked: panel.close()
        }
    }
}
