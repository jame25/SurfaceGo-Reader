import QtQuick
import QtQuick.Controls

// Rounded, touch-sized button with a plain (shader-free) background, so it
// renders identically with GPU and software rendering.
Button {
    id: control
    property bool accent: true   // filled accent colour vs. outlined

    implicitHeight: Theme.touch
    leftPadding: 22
    rightPadding: 22
    font.pixelSize: 17
    focusPolicy: Qt.NoFocus
    contentItem: Label {
        text: control.text
        font: control.font
        color: control.accent ? "white" : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: 64
        radius: height / 2
        color: control.accent ? Theme.accent : "transparent"
        border.width: control.accent ? 0 : 1
        border.color: Theme.divider
        opacity: control.enabled ? (control.pressed ? 0.8 : 1.0) : 0.4
    }
}
