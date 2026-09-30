import QtQuick
import QtQuick.Controls

// Tool button sized for fingers, with a bundled monochrome SVG icon.
ToolButton {
    id: control
    property string iconName: ""
    property string label: ""      // accessible name
    property int iconSize: 28

    implicitWidth: Math.max(Theme.touch, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Theme.touch
    focusPolicy: Qt.NoFocus
    icon.source: iconName.length > 0 ? Qt.resolvedUrl("icons/" + iconName + ".svg") : ""
    icon.width: iconSize
    icon.height: iconSize
    icon.color: checked ? Theme.accent : Theme.text
    display: iconName.length > 0 && text.length === 0 ? AbstractButton.IconOnly
           : iconName.length > 0 ? AbstractButton.TextBesideIcon : AbstractButton.TextOnly
    font.pixelSize: 20
    Accessible.name: label.length > 0 ? label : text
}
