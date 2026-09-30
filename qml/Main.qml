import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

ApplicationWindow {
    id: window
    // Size and state come from the last session; see restoreGeometry().
    title: reader.bookOpen ? reader.title + " — SurfaceGo Reader" : "SurfaceGo Reader"
    color: Theme.background

    Material.theme: Theme.dark ? Material.Dark : Material.Light
    Material.accent: Theme.accent
    Material.primary: Theme.accent
    Material.background: Theme.surface

    Binding { target: Theme; property: "name"; value: appSettings.theme }
    Binding { target: Theme; property: "highlight"; value: appSettings.highlightColor }
    Binding { target: reader; property: "sentenceColor"; value: Theme.sentence }
    Binding { target: reader; property: "sentenceTextColor"; value: Theme.sentenceText }

    // Window size and state are restored from the last session. The size is
    // only recorded while windowed, so a maximized or full-screen window
    // still returns to its previous normal size.
    function restoreGeometry() {
        const maxW = Screen.desktopAvailableWidth > 0 ? Screen.desktopAvailableWidth : appSettings.windowWidth
        const maxH = Screen.desktopAvailableHeight > 0 ? Screen.desktopAvailableHeight : appSettings.windowHeight
        width = Math.min(appSettings.windowWidth, maxW)
        height = Math.min(appSettings.windowHeight, maxH)
        if (appSettings.windowMode === "fullscreen")
            showFullScreen()
        else if (appSettings.windowMode === "maximized")
            showMaximized()
        else
            show()
    }
    function saveGeometry() {
        if (visibility === Window.Windowed) {
            appSettings.windowWidth = width
            appSettings.windowHeight = height
            appSettings.windowMode = "normal"
        } else if (visibility === Window.Maximized) {
            appSettings.windowMode = "maximized"
        } else if (visibility === Window.FullScreen) {
            appSettings.windowMode = "fullscreen"
        }
    }
    Component.onCompleted: restoreGeometry()
    onClosing: saveGeometry()
    // Also save while running (debounced), in case the session ends without
    // the window being closed.
    onWidthChanged: geometryTimer.restart()
    onHeightChanged: geometryTimer.restart()
    onVisibilityChanged: geometryTimer.restart()
    Timer { id: geometryTimer; interval: 1000; onTriggered: window.saveGeometry() }

    function showMessage(text) {
        toastLabel.text = text
        toast.open()
        toastTimer.restart()
    }

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: libraryComponent
    }

    Component {
        id: libraryComponent
        LibraryPage {
            onOpenBook: path => {
                reader.openBook(path)
                if (stack.depth === 1)
                    stack.push(readerComponent)
            }
            onOpenSettings: stack.push(settingsComponent)
        }
    }

    Component {
        id: readerComponent
        ReaderPage {
            onBack: {
                reader.closeBook()
                stack.pop(null)
            }
            onOpenSettings: stack.push(settingsComponent)
        }
    }

    Component {
        id: settingsComponent
        SettingsPage {
            onBack: stack.pop()
        }
    }

    Connections {
        target: reader
        function onErrorOccurred(message) {
            window.showMessage(message)
            // A book that failed to load leaves the (empty) reader page.
            if (!reader.bookOpen && !reader.loading && stack.depth > 1)
                stack.pop(null)
        }
        function onLoadingChanged() {
            // Book passed on the command line
            if (reader.loading && stack.depth === 1)
                stack.push(readerComponent)
        }
    }

    Connections {
        target: library
        function onMessage(text) { window.showMessage(text) }
    }

    Shortcut {
        sequences: [StandardKey.Back, "Esc"]
        enabled: stack.depth > 1
        onActivated: {
            if (stack.currentItem && stack.currentItem.back)
                stack.currentItem.back()
        }
    }
    Shortcut {
        sequence: "F11"
        onActivated: window.visibility = (window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen)
    }

    Popup {
        id: toast
        x: (window.width - width) / 2
        y: window.height - height - 120
        width: Math.min(window.width - 32, toastLabel.implicitWidth + 48)
        padding: 16
        closePolicy: Popup.CloseOnPressOutside
        background: Rectangle { radius: 12; color: Theme.dark ? "#424242" : "#323232"; opacity: 0.95 }
        contentItem: Label {
            id: toastLabel
            color: "white"
            wrapMode: Text.Wrap
            font.pixelSize: 18
            horizontalAlignment: Text.AlignHCenter
        }
        Timer { id: toastTimer; interval: 4500; onTriggered: toast.close() }
    }
}
