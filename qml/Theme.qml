pragma Singleton
import QtQuick

QtObject {
    // Page theme: "light", "sepia", "dark" or "black"
    property string name: "light"
    // Spoken-sentence highlight: "green", "yellow", "blue", "pink" or "orange"
    property string highlight: "green"

    readonly property bool dark: name === "dark" || name === "black"

    readonly property var themes: ({
        light: { background: "#fbfaf6", surface: "#ffffff", bar: "#f1efe9", text: "#1b1b1b",
                 subtext: "#6b6b6b", divider: "#dedbd2", accent: "#2e7d32" },
        sepia: { background: "#f4ecd8", surface: "#faf4e4", bar: "#eae0c8", text: "#3b2e1e",
                 subtext: "#7a6a52", divider: "#d9ccae", accent: "#8b5a2b" },
        dark:  { background: "#121212", surface: "#1e1e1e", bar: "#1b1b1b", text: "#e4e4e4",
                 subtext: "#9e9e9e", divider: "#333333", accent: "#66bb6a" },
        black: { background: "#000000", surface: "#121212", bar: "#0a0a0a", text: "#d6d6d6",
                 subtext: "#8a8a8a", divider: "#262626", accent: "#66bb6a" }
    })
    readonly property var themeNames: ["light", "sepia", "dark", "black"]

    // Sentence background / sentence text, for light and dark pages.
    readonly property var highlights: ({
        green:  { light: ["#b5e6b9", "#0d1f0e"], dark: ["#2f6b36", "#ffffff"] },
        yellow: { light: ["#fff176", "#1b1b1b"], dark: ["#d4b106", "#121212"] },
        blue:   { light: ["#bbdefb", "#0d1b2a"], dark: ["#1e4f7a", "#ffffff"] },
        pink:   { light: ["#f8bbd0", "#2a0d17"], dark: ["#7a2e4d", "#ffffff"] },
        orange: { light: ["#ffcc80", "#2a1a05"], dark: ["#b35f00", "#ffffff"] }
    })
    readonly property var highlightNames: ["green", "yellow", "blue", "pink", "orange"]

    readonly property var palette: themes[name] || themes.light
    readonly property var sentencePair: (highlights[highlight] || highlights.green)[dark ? "dark" : "light"]

    readonly property color background: palette.background
    readonly property color surface: palette.surface
    readonly property color bar: palette.bar
    readonly property color text: palette.text
    readonly property color subtext: palette.subtext
    readonly property color divider: palette.divider
    readonly property color sentence: sentencePair[0]
    readonly property color sentenceText: sentencePair[1]
    readonly property color accent: palette.accent
    readonly property color epub: "#3d7a5a"
    readonly property color pdf: "#a8483d"

    // Minimum touch target (logical px; Qt scales for the Surface's 150% display)
    readonly property int touch: 56
    readonly property int margin: 16
}
