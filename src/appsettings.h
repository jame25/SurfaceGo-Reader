#pragma once

#include <QObject>
#include <QSettings>
#include <QVariantMap>

// Persistent application settings (~/.config/surfacego-reader/surfacego-reader.conf)
// plus per-book reading state.
class AppSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString libraryFolder READ libraryFolder WRITE setLibraryFolder NOTIFY libraryFolderChanged)
    Q_PROPERTY(QString voicesFolder READ voicesFolder WRITE setVoicesFolder NOTIFY voicesFolderChanged)
    Q_PROPERTY(QString voice READ voice WRITE setVoice NOTIFY voiceChanged)
    Q_PROPERTY(int speaker READ speaker WRITE setSpeaker NOTIFY speakerChanged)
    Q_PROPERTY(double lengthScale READ lengthScale WRITE setLengthScale NOTIFY lengthScaleChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(bool wordWrap READ wordWrap WRITE setWordWrap NOTIFY wordWrapChanged)
    Q_PROPERTY(bool autoScroll READ autoScroll WRITE setAutoScroll NOTIFY autoScrollChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString highlightColor READ highlightColor WRITE setHighlightColor NOTIFY highlightColorChanged)
    Q_PROPERTY(QString wordHighlight READ wordHighlight WRITE setWordHighlight NOTIFY wordHighlightChanged)
    Q_PROPERTY(int latencyOffsetMs READ latencyOffsetMs WRITE setLatencyOffsetMs NOTIFY latencyOffsetMsChanged)
    Q_PROPERTY(bool sortByRecent READ sortByRecent WRITE setSortByRecent NOTIFY sortByRecentChanged)
    // Main window size in windowed mode, and "normal", "maximized" or "fullscreen"
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY windowWidthChanged)
    Q_PROPERTY(int windowHeight READ windowHeight WRITE setWindowHeight NOTIFY windowHeightChanged)
    Q_PROPERTY(QString windowMode READ windowMode WRITE setWindowMode NOTIFY windowModeChanged)

public:
    explicit AppSettings(QObject *parent = nullptr);

    QString libraryFolder() const;
    void setLibraryFolder(const QString &v);
    QString voicesFolder() const;
    void setVoicesFolder(const QString &v);
    QString voice() const;
    void setVoice(const QString &v);
    int speaker() const;
    void setSpeaker(int v);
    double lengthScale() const;
    void setLengthScale(double v);
    int fontSize() const;
    void setFontSize(int v);
    bool wordWrap() const;
    void setWordWrap(bool v);
    bool autoScroll() const;
    void setAutoScroll(bool v);
    QString theme() const;          // "light", "sepia", "dark", "black"
    void setTheme(const QString &v);
    QString highlightColor() const; // "green", "yellow", "blue", "pink", "orange"
    void setHighlightColor(const QString &v);
    QString wordHighlight() const;  // "bold", "inverted"
    void setWordHighlight(const QString &v);
    int latencyOffsetMs() const;
    void setLatencyOffsetMs(int v);
    bool sortByRecent() const;
    void setSortByRecent(bool v);
    int windowWidth() const;
    void setWindowWidth(int v);
    int windowHeight() const;
    void setWindowHeight(int v);
    QString windowMode() const;
    void setWindowMode(const QString &v);

    // Per-book state: sentence, total, viewParagraph, lastOpened, title, author
    QVariantMap bookState(const QString &key) const;
    void setBookState(const QString &key, const QVariantMap &state);
    void removeBookState(const QString &key);

    Q_INVOKABLE QString homePath() const;
    Q_INVOKABLE bool ensureFolder(const QString &path) const;

Q_SIGNALS:
    void libraryFolderChanged();
    void voicesFolderChanged();
    void voiceChanged();
    void speakerChanged();
    void lengthScaleChanged();
    void fontSizeChanged();
    void wordWrapChanged();
    void autoScrollChanged();
    void themeChanged();
    void highlightColorChanged();
    void wordHighlightChanged();
    void latencyOffsetMsChanged();
    void sortByRecentChanged();
    void windowWidthChanged();
    void windowHeightChanged();
    void windowModeChanged();

private:
    template<typename T>
    bool store(const char *key, const T &value, const T &current);

    mutable QSettings m_settings;
};
