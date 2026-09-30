import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: page
    signal back()
    signal openSettings()

    background: Rectangle { color: Theme.background }

    // Auto-scroll is suspended while the user is scrolling by hand, and for a
    // few seconds afterwards.
    property bool userHold: false
    property int pinchBaseSize: appSettings.fontSize
    readonly property int textMargin: Math.max(Theme.margin, Math.round(page.width * 0.04))
    readonly property font bodyFont: Qt.font({ family: "serif", pixelSize: appSettings.fontSize })
    readonly property font headingFont: Qt.font({ family: "serif", pixelSize: Math.round(appSettings.fontSize * 1.3), bold: true })

    Binding { target: reader; property: "textFont"; value: page.bodyFont }

    header: ToolBar {
        height: Theme.touch + 8
        background: Rectangle { color: Theme.bar }
        RowLayout {
            anchors.fill: parent
            spacing: 0
            TouchButton { iconName: "back"; label: "Back to library"; onClicked: page.back() }
            TouchButton { iconName: "toc"; label: "Contents"; onClicked: tocDrawer.open(); enabled: reader.bookOpen }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: reader.title
                    elide: Text.ElideRight
                    font.pixelSize: 19
                    font.bold: true
                    color: Theme.text
                }
                Label {
                    Layout.fillWidth: true
                    visible: reader.bookOpen
                    text: {
                        const ch = reader.currentChapter
                        const chapters = reader.chapters
                        const name = ch >= 0 && ch < chapters.length ? chapters[ch].title : ""
                        return Math.round(reader.progress * 100) + "%" + (name.length ? "  ·  " + name : "")
                    }
                    elide: Text.ElideRight
                    font.pixelSize: 14
                    color: Theme.subtext
                }
            }
            TouchButton {
                text: "A−"
                label: "Smaller text"
                font.pixelSize: 18
                onClicked: appSettings.fontSize = appSettings.fontSize - 2
            }
            TouchButton {
                text: "A+"
                label: "Larger text"
                font.pixelSize: 24
                onClicked: appSettings.fontSize = appSettings.fontSize + 2
            }
            TouchButton {
                iconName: "wrap"
                label: "Word wrap"
                checkable: true
                checked: appSettings.wordWrap
                onToggled: appSettings.wordWrap = checked
            }
            TouchButton {
                iconName: "follow"
                label: "Auto-scroll"
                checkable: true
                checked: appSettings.autoScroll
                onToggled: {
                    appSettings.autoScroll = checked
                    if (checked) {
                        page.userHold = false
                        page.followCurrent(true)
                    }
                }
            }
            TouchButton { iconName: "settings"; label: "Settings"; onClicked: page.openSettings() }
        }
    }

    ListView {
        id: view
        anchors.fill: parent
        clip: true
        model: reader.paragraphs
        cacheBuffer: 1200
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: appSettings.wordWrap ? Flickable.VerticalFlick : Flickable.AutoFlickDirection
        contentWidth: appSettings.wordWrap ? width : Math.max(width, widest)
        pixelAligned: true
        maximumFlickVelocity: 6000
        topMargin: 12
        bottomMargin: Math.round(height * 0.4) // lets the last sentence scroll up to reading height

        property real widest: width

        ScrollBar.vertical: ScrollBar {
            width: 12
            minimumSize: 0.04
        }
        ScrollBar.horizontal: ScrollBar {
            visible: !appSettings.wordWrap
            height: 12
        }

        delegate: Item {
            id: para
            required property int index
            required property string text
            required property bool heading
            readonly property bool active: index === reader.currentParagraph
            property alias textItem: body

            width: appSettings.wordWrap ? view.width : Math.max(view.width, body.implicitWidth + 2 * page.textMargin)
            height: body.height + (heading ? appSettings.fontSize * 1.6 : appSettings.fontSize * 0.7)

            onWidthChanged: if (!appSettings.wordWrap && width > view.widest) view.widest = width

            TextEdit {
                id: body
                x: page.textMargin
                y: para.heading ? appSettings.fontSize * 1.1 : appSettings.fontSize * 0.35
                width: appSettings.wordWrap ? para.width - 2 * page.textMargin : implicitWidth
                enabled: false          // display only; taps are handled below
                readOnly: true
                selectByMouse: false
                activeFocusOnPress: false
                textFormat: para.active ? TextEdit.RichText : TextEdit.PlainText
                text: para.active ? reader.highlightHtml : para.text
                wrapMode: appSettings.wordWrap ? TextEdit.Wrap : TextEdit.NoWrap
                color: Theme.text
                font: para.heading ? page.headingFont : page.bodyFont
                horizontalAlignment: para.heading ? TextEdit.AlignHCenter : TextEdit.AlignLeft
            }

            // Tap a word: start reading from its sentence.
            TapHandler {
                gesturePolicy: TapHandler.DragThreshold
                onTapped: (eventPoint, button) => {
                    const p = body.mapFromItem(para, eventPoint.position.x, eventPoint.position.y)
                    const pos = body.positionAt(p.x, p.y)
                    page.userHold = false
                    reader.playFromPosition(para.index, pos)
                }
            }
        }

        onMovementStarted: {
            if (!scrollAnim.running) {
                page.userHold = true
                holdTimer.stop()
            }
        }
        onMovementEnded: {
            holdTimer.restart()
            saveTimer.restart()
            page.updateCurrentVisible()
        }
        onContentYChanged: visibleTimer.restart()
        onHeightChanged: page.followCurrent(true)

        PinchHandler {
            target: null
            minimumPointCount: 2
            maximumPointCount: 2
            onActiveChanged: if (active) page.pinchBaseSize = appSettings.fontSize
            onActiveScaleChanged: {
                if (active)
                    appSettings.fontSize = Math.round(page.pinchBaseSize * activeScale)
            }
        }
    }

    NumberAnimation {
        id: scrollAnim
        target: view
        property: "contentY"
        duration: 380
        easing.type: Easing.InOutQuad
    }

    Timer { id: holdTimer; interval: 5000; onTriggered: page.userHold = false }
    Timer {
        id: saveTimer
        interval: 800
        onTriggered: {
            const idx = view.indexAt(20, view.contentY + 20)
            if (idx >= 0)
                reader.setViewParagraph(idx)
        }
    }
    Timer { id: visibleTimer; interval: 150; onTriggered: page.updateCurrentVisible() }

    property bool currentVisible: true
    function updateCurrentVisible() {
        const p = reader.currentParagraph
        if (p < 0) { currentVisible = true; return }
        const top = view.indexAt(20, view.contentY + 5)
        const bottom = view.indexAt(20, view.contentY + view.height - 5)
        currentVisible = (top < 0 || p >= top) && (bottom < 0 || p <= bottom)
    }

    // Keep the spoken line comfortably inside the viewport.
    function followCurrent(force) {
        const p = reader.currentParagraph
        if (p < 0 || !reader.bookOpen)
            return
        let item = view.itemAtIndex(p)
        if (!item) {
            scrollAnim.stop()
            view.positionViewAtIndex(p, ListView.Beginning)
            item = view.itemAtIndex(p)
            if (!item)
                return
        }
        const body = item.textItem
        const r = body.positionToRectangle(reader.wordStart)
        const lineTop = item.y + body.y + r.y
        const lineBottom = lineTop + r.height
        const viewTop = view.contentY
        const h = view.height
        if (!force && lineTop >= viewTop + h * 0.12 && lineBottom <= viewTop + h * 0.72)
            return
        const minY = view.originY - view.topMargin
        const maxY = Math.max(minY, view.originY + view.contentHeight + view.bottomMargin - h)
        const target = Math.max(minY, Math.min(maxY, lineTop - h * 0.28))
        if (Math.abs(target - view.contentY) < 2)
            return
        scrollAnim.stop()
        scrollAnim.from = view.contentY
        scrollAnim.to = target
        scrollAnim.start()
    }

    Connections {
        target: reader
        function onHighlightChanged() {
            if (appSettings.autoScroll && !page.userHold && (reader.playing || reader.paused) && !view.dragging && !view.flicking)
                page.followCurrent(false)
            visibleTimer.restart()
        }
        function onBookChanged() {
            if (reader.bookOpen)
                Qt.callLater(page.restoreView)
        }
    }

    function restoreView() {
        view.widest = view.width
        view.positionViewAtIndex(reader.initialViewParagraph, ListView.Beginning)
        updateCurrentVisible()
    }
    Component.onCompleted: if (reader.bookOpen) Qt.callLater(restoreView)

    // Re-anchor after layout-changing settings.
    Connections {
        target: appSettings
        function onFontSizeChanged() { relayoutTimer.restart() }
        function onWordWrapChanged() { view.widest = view.width; relayoutTimer.restart() }
    }
    Timer {
        id: relayoutTimer
        interval: 60
        onTriggered: {
            if (reader.currentParagraph >= 0 && page.currentVisible)
                page.followCurrent(true)
        }
    }

    // "Back to the spoken sentence" pill when it has scrolled out of view.
    PillButton {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 16 }
        visible: reader.bookOpen && !page.currentVisible && reader.currentParagraph >= 0
        text: "\u2316  Back to current sentence"
        onClicked: {
            page.userHold = false
            page.followCurrent(true)
        }
    }

    // Loading overlay
    Rectangle {
        anchors.fill: parent
        color: Theme.background
        visible: reader.loading
        ColumnLayout {
            anchors.centerIn: parent
            spacing: 16
            BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: reader.loading }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "Preparing book… " + reader.loadProgress + "%"
                font.pixelSize: 20
                color: Theme.text
            }
            ProgressBar {
                Layout.preferredWidth: 320
                from: 0; to: 100
                value: reader.loadProgress
            }
        }
    }

    footer: ToolBar {
        height: 96
        background: Rectangle {
            color: Theme.bar
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.divider }
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8

            TouchButton {
                iconName: "voice"
                label: "Voice and speed"
                text: page.width > 900 ? (reader.voiceLoading ? "Loading…" : voiceName()) : ""
                font.pixelSize: 16
                onClicked: voicePanel.open()
                function voiceName() {
                    const i = voiceList.indexOfPath(appSettings.voice)
                    return i >= 0 ? voiceList.voices[i].name : "Voice"
                }
            }
            TouchButton {
                iconName: "speed"
                text: (1 / appSettings.lengthScale).toFixed(2) + "×"
                label: "Speed"
                font.pixelSize: 16
                onClicked: voicePanel.open()
            }

            Item { Layout.fillWidth: true }

            TouchButton {
                iconName: "previous"
                label: "Previous sentence"
                iconSize: 36
                implicitWidth: 72; implicitHeight: 72
                enabled: reader.bookOpen && reader.currentSentence > 0
                onClicked: reader.previousSentence()
            }
            RoundButton {
                id: playButton
                implicitWidth: 84
                implicitHeight: 84
                highlighted: true
                enabled: reader.bookOpen
                focusPolicy: Qt.NoFocus
                icon.source: reader.playing ? "icons/pause.svg" : "icons/play.svg"
                icon.width: 44
                icon.height: 44
                icon.color: "white"
                background: Rectangle {
                    radius: width / 2
                    color: playButton.enabled ? Theme.accent : Theme.divider
                    opacity: playButton.pressed ? 0.8 : 1.0
                }
                Accessible.name: reader.playing ? "Pause" : "Play"
                onClicked: reader.togglePlay()
                BusyIndicator {
                    anchors.fill: parent
                    running: reader.buffering && reader.playing
                    visible: running
                }
            }
            TouchButton {
                iconName: "stop"
                label: "Stop"
                iconSize: 36
                implicitWidth: 72; implicitHeight: 72
                enabled: reader.playing || reader.paused
                onClicked: reader.stop()
            }
            TouchButton {
                iconName: "next"
                label: "Next sentence"
                iconSize: 36
                implicitWidth: 72; implicitHeight: 72
                enabled: reader.bookOpen && reader.currentSentence < reader.sentenceCount - 1
                onClicked: reader.nextSentence()
            }

            Item { Layout.fillWidth: true }

            Label {
                Layout.preferredWidth: page.width > 900 ? 200 : 0
                visible: page.width > 900
                horizontalAlignment: Text.AlignRight
                text: reader.voiceError.length ? "⚠ Voice unavailable"
                    : reader.voiceLoading ? "Loading voice…"
                    : reader.buffering && reader.playing ? "Preparing speech…"
                    : reader.paused ? "Paused" : ""
                color: reader.voiceError.length ? "#e53935" : Theme.subtext
                font.pixelSize: 15
                elide: Text.ElideRight
            }
        }
    }

    Drawer {
        id: tocDrawer
        parent: Overlay.overlay
        edge: Qt.LeftEdge
        width: Math.min(page.width * 0.85, 460)
        height: parent ? parent.height : page.height
        background: Rectangle { color: Theme.surface }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            Label {
                Layout.fillWidth: true
                Layout.margins: Theme.margin
                text: "Contents"
                font.pixelSize: 24
                font.bold: true
                color: Theme.text
            }
            ListView {
                id: tocList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: reader.chapters
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { width: 10 }
                delegate: ItemDelegate {
                    required property int index
                    required property var modelData
                    width: tocList.width
                    implicitHeight: Theme.touch
                    leftPadding: Theme.margin + modelData.level * 20
                    highlighted: index === reader.currentChapter
                    text: modelData.title
                    font.pixelSize: 17
                    onClicked: {
                        tocDrawer.close()
                        page.userHold = true
                        holdTimer.restart()
                        scrollAnim.stop()
                        view.positionViewAtIndex(modelData.paragraph, ListView.Beginning)
                        saveTimer.restart()
                    }
                }
                onVisibleChanged: if (visible && reader.currentChapter >= 0) positionViewAtIndex(reader.currentChapter, ListView.Center)
            }
        }
    }

    VoicePanel {
        id: voicePanel
        width: Math.min(page.width, 720)
        x: (page.width - width) / 2
    }
}
