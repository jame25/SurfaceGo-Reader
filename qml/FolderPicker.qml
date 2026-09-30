import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.folderlistmodel

// Full-screen, finger-friendly folder chooser.
Popup {
    id: picker
    parent: Overlay.overlay
    width: parent ? parent.width : 800
    height: parent ? parent.height : 600
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    property string title: "Choose folder"
    property string currentPath: "/"
    property var callback: null

    function pick(title, startPath, cb) {
        picker.title = title
        picker.callback = cb
        picker.currentPath = startPath && startPath.length ? startPath : appSettings.homePath()
        open()
    }

    function toUrl(path) {
        return "file://" + path.split("/").map(encodeURIComponent).join("/")
    }

    function parentOf(path) {
        const i = path.lastIndexOf("/")
        return i <= 0 ? "/" : path.substring(0, i)
    }

    background: Rectangle { color: Theme.background }

    FolderListModel {
        id: folders
        folder: picker.toUrl(picker.currentPath)
        showFiles: false
        showDirs: true
        showDotAndDotDot: false
        showHidden: false
        sortCaseSensitive: false
    }

    contentItem: ColumnLayout {
        spacing: 0

        ToolBar {
            Layout.fillWidth: true
            implicitHeight: Theme.touch + 8
            background: Rectangle { color: Theme.bar }
            RowLayout {
                anchors.fill: parent
                TouchButton { iconName: "back"; label: "Cancel"; onClicked: picker.close() }
                Label {
                    text: picker.title
                    font.pixelSize: 22
                    font.bold: true
                    color: Theme.text
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            TouchButton {
                iconName: "up"
                label: "Parent folder"
                enabled: picker.currentPath !== "/"
                onClicked: picker.currentPath = picker.parentOf(picker.currentPath)
            }
            TouchButton {
                text: "Home"
                font.pixelSize: 16
                onClicked: picker.currentPath = appSettings.homePath()
            }
            TouchButton {
                text: "Media"
                font.pixelSize: 16
                onClicked: picker.currentPath = "/run/media"
            }
            Label {
                Layout.fillWidth: true
                text: picker.currentPath
                elide: Text.ElideLeft
                font.pixelSize: 16
                color: Theme.subtext
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: folders
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { width: 10 }
            delegate: ItemDelegate {
                required property string fileName
                required property string filePath
                width: list.width
                implicitHeight: Theme.touch + 4
                font.pixelSize: 18
                text: fileName
                icon.source: "icons/folder.svg"
                icon.color: Theme.accent
                onClicked: picker.currentPath = filePath
            }
            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: "No sub-folders"
                color: Theme.subtext
                font.pixelSize: 17
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 12
            Item { Layout.fillWidth: true }
            Button {
                text: "Cancel"
                flat: true
                implicitHeight: Theme.touch
                onClicked: picker.close()
            }
            PillButton {
                text: "Use this folder"
                onClicked: {
                    if (picker.callback)
                        picker.callback(picker.currentPath)
                    picker.close()
                }
            }
        }
    }
}
