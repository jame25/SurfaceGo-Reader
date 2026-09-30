import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: page
    signal openBook(string path)
    signal openSettings()

    background: Rectangle { color: Theme.background }

    header: ToolBar {
        height: Theme.touch + 8
        background: Rectangle { color: Theme.bar }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.margin
            spacing: 4
            Label {
                text: "Library"
                font.pixelSize: 26
                font.bold: true
                color: Theme.text
            }
            Label {
                text: library.count + (library.count === 1 ? " book" : " books")
                color: Theme.subtext
                font.pixelSize: 16
                Layout.leftMargin: 8
            }
            BusyIndicator {
                running: library.scanning
                visible: running
                Layout.preferredHeight: 32
                Layout.preferredWidth: 32
            }
            Item { Layout.fillWidth: true }
            TouchButton {
                iconName: "sort"
                text: appSettings.sortByRecent ? "Recent" : "Title"
                label: "Sort order"
                onClicked: appSettings.sortByRecent = !appSettings.sortByRecent
            }
            TouchButton {
                iconName: "refresh"
                label: "Rescan library"
                onClicked: library.refresh()
            }
            TouchButton {
                iconName: "settings"
                label: "Settings"
                onClicked: page.openSettings()
            }
        }
    }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.margins: Theme.margin / 2
        clip: true
        model: library
        boundsBehavior: Flickable.StopAtBounds
        readonly property int columns: Math.max(2, Math.floor(width / 230))
        cellWidth: Math.floor(width / columns)
        cellHeight: Math.round(cellWidth * 1.3)
        ScrollBar.vertical: ScrollBar { width: 10 }

        delegate: Item {
            id: cell
            required property int index
            required property string path
            required property string title
            required property string author
            required property string format
            required property double progress
            required property string fileName

            width: grid.cellWidth
            height: grid.cellHeight

            Rectangle {
                id: card
                anchors.fill: parent
                anchors.margins: 8
                radius: 10
                color: Theme.surface
                border.color: Theme.divider
                scale: tap.pressed ? 0.96 : 1.0
                Behavior on scale { NumberAnimation { duration: 90 } }

                // "Cover": coloured block with the title
                Rectangle {
                    id: cover
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    height: parent.height * 0.68
                    radius: 10
                    color: cell.format === "pdf" ? Theme.pdf : Theme.epub
                    Rectangle { // square off the bottom corners
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 10
                        color: parent.color
                    }
                    Label {
                        anchors.fill: parent
                        anchors.margins: 14
                        text: cell.title
                        color: "white"
                        font.pixelSize: 19
                        font.bold: true
                        wrapMode: Text.Wrap
                        elide: Text.ElideRight
                        maximumLineCount: 5
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Label {
                        anchors { right: parent.right; top: parent.top; margins: 8 }
                        text: cell.format.toUpperCase()
                        color: "white"
                        opacity: 0.8
                        font.pixelSize: 12
                        font.bold: true
                    }
                }

                ColumnLayout {
                    anchors { left: parent.left; right: parent.right; top: cover.bottom; bottom: parent.bottom; margins: 10 }
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        text: cell.author.length > 0 ? cell.author : cell.fileName
                        color: Theme.subtext
                        elide: Text.ElideRight
                        font.pixelSize: 15
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.fillWidth: true
                        ProgressBar {
                            Layout.fillWidth: true
                            from: 0; to: 1
                            value: cell.progress
                        }
                        Label {
                            text: Math.round(cell.progress * 100) + "%"
                            color: Theme.subtext
                            font.pixelSize: 14
                        }
                    }
                }

                TapHandler {
                    id: tap
                    longPressThreshold: 0.6
                    onTapped: page.openBook(cell.path)
                    onLongPressed: {
                        removeDialog.bookPath = cell.path
                        removeDialog.bookTitle = cell.title
                        removeDialog.open()
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 560)
        visible: library.count === 0 && !library.scanning
        spacing: 16
        TouchButton {
            Layout.alignment: Qt.AlignHCenter
            iconName: "book"
            iconSize: 96
            implicitWidth: 120
            implicitHeight: 120
            opacity: 0.35
            enabled: false
        }
        Label {
            Layout.fillWidth: true
            text: "No books yet"
            font.pixelSize: 26
            color: Theme.text
            horizontalAlignment: Text.AlignHCenter
        }
        Label {
            Layout.fillWidth: true
            text: "Copy EPUB or PDF files into\n" + appSettings.libraryFolder + "\nor choose another library folder in Settings."
            wrapMode: Text.Wrap
            font.pixelSize: 17
            color: Theme.subtext
            horizontalAlignment: Text.AlignHCenter
        }
        PillButton {
            Layout.alignment: Qt.AlignHCenter
            text: "Open Settings"
            onClicked: page.openSettings()
        }
    }

    Dialog {
        id: removeDialog
        property string bookPath
        property string bookTitle
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: Math.max(300, Math.min(page.width - 48, 520))
        modal: true
        title: "Remove book?"
        Material.elevation: 0
        // Explicit content width avoids a wrap-width/implicit-size binding loop.
        contentWidth: width - leftPadding - rightPadding
        Label {
            width: removeDialog.contentWidth
            wrapMode: Text.Wrap
            font.pixelSize: 18
            text: "\u201C" + removeDialog.bookTitle + "\u201D will be moved to the trash and its reading position forgotten."
        }
        footer: DialogButtonBox {
            alignment: Qt.AlignRight
            Button {
                text: "Cancel"
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                text: "Remove"
                flat: true
                Material.foreground: "#c62828"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: {
            if (reader.bookPath === bookPath)
                reader.closeBook()
            library.removeBook(bookPath)
        }
    }
}
