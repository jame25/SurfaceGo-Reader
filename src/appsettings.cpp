#include "appsettings.h"

#include <QDir>
#include <QStandardPaths>

namespace {
QString defaultLibrary()
{
    return QDir::homePath() + QStringLiteral("/Books");
}
QString defaultVoices()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/surfacego-reader/voices");
}

// Allowed values of the string settings; the first one is the default.
const QStringList kThemes = {QStringLiteral("light"), QStringLiteral("sepia"), QStringLiteral("dark"), QStringLiteral("black")};
const QStringList kHighlightColors = {QStringLiteral("green"), QStringLiteral("yellow"), QStringLiteral("blue"),
                                      QStringLiteral("pink"), QStringLiteral("orange")};
const QStringList kWordHighlights = {QStringLiteral("bold"), QStringLiteral("inverted")};
const QStringList kWindowModes = {QStringLiteral("normal"), QStringLiteral("maximized"), QStringLiteral("fullscreen")};

QString oneOf(const QString &value, const QStringList &allowed)
{
    return allowed.contains(value) ? value : allowed.first();
}
}

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(libraryFolder());
    QDir().mkpath(voicesFolder());
}

template<typename T>
bool AppSettings::store(const char *key, const T &value, const T &current)
{
    if (value == current)
        return false;
    m_settings.setValue(QLatin1String(key), QVariant::fromValue(value));
    return true;
}

QString AppSettings::libraryFolder() const { return m_settings.value("libraryFolder", defaultLibrary()).toString(); }
void AppSettings::setLibraryFolder(const QString &v)
{
    if (store("libraryFolder", v, libraryFolder()))
        Q_EMIT libraryFolderChanged();
}

QString AppSettings::voicesFolder() const { return m_settings.value("voicesFolder", defaultVoices()).toString(); }
void AppSettings::setVoicesFolder(const QString &v)
{
    if (store("voicesFolder", v, voicesFolder()))
        Q_EMIT voicesFolderChanged();
}

QString AppSettings::voice() const { return m_settings.value("voice").toString(); }
void AppSettings::setVoice(const QString &v)
{
    if (store("voice", v, voice()))
        Q_EMIT voiceChanged();
}

int AppSettings::speaker() const { return m_settings.value("speaker", 0).toInt(); }
void AppSettings::setSpeaker(int v)
{
    if (store("speaker", v, speaker()))
        Q_EMIT speakerChanged();
}

double AppSettings::lengthScale() const { return m_settings.value("lengthScale", 1.0).toDouble(); }
void AppSettings::setLengthScale(double v)
{
    v = qBound(0.3, qRound(v * 100) / 100.0, 3.0);
    if (store("lengthScale", v, lengthScale()))
        Q_EMIT lengthScaleChanged();
}

int AppSettings::fontSize() const { return m_settings.value("fontSize", 24).toInt(); }
void AppSettings::setFontSize(int v)
{
    v = qBound(10, v, 72);
    if (store("fontSize", v, fontSize()))
        Q_EMIT fontSizeChanged();
}

bool AppSettings::wordWrap() const { return m_settings.value("wordWrap", true).toBool(); }
void AppSettings::setWordWrap(bool v)
{
    if (store("wordWrap", v, wordWrap()))
        Q_EMIT wordWrapChanged();
}

bool AppSettings::autoScroll() const { return m_settings.value("autoScroll", true).toBool(); }
void AppSettings::setAutoScroll(bool v)
{
    if (store("autoScroll", v, autoScroll()))
        Q_EMIT autoScrollChanged();
}

QString AppSettings::theme() const
{
    // Before 1.1 there was only a dark-theme switch.
    const QString fallback = m_settings.value("darkTheme", false).toBool() ? QStringLiteral("dark") : QStringLiteral("light");
    return oneOf(m_settings.value("theme", fallback).toString(), kThemes);
}
void AppSettings::setTheme(const QString &v)
{
    if (!kThemes.contains(v))
        return;
    if (store("theme", v, theme()))
        Q_EMIT themeChanged();
}

QString AppSettings::highlightColor() const { return oneOf(m_settings.value("highlightColor").toString(), kHighlightColors); }
void AppSettings::setHighlightColor(const QString &v)
{
    if (!kHighlightColors.contains(v))
        return;
    if (store("highlightColor", v, highlightColor()))
        Q_EMIT highlightColorChanged();
}

QString AppSettings::wordHighlight() const { return oneOf(m_settings.value("wordHighlight").toString(), kWordHighlights); }
void AppSettings::setWordHighlight(const QString &v)
{
    if (!kWordHighlights.contains(v))
        return;
    if (store("wordHighlight", v, wordHighlight()))
        Q_EMIT wordHighlightChanged();
}

int AppSettings::latencyOffsetMs() const { return m_settings.value("latencyOffsetMs", 0).toInt(); }
void AppSettings::setLatencyOffsetMs(int v)
{
    v = qBound(-500, v, 1000);
    if (store("latencyOffsetMs", v, latencyOffsetMs()))
        Q_EMIT latencyOffsetMsChanged();
}

bool AppSettings::sortByRecent() const { return m_settings.value("sortByRecent", true).toBool(); }
void AppSettings::setSortByRecent(bool v)
{
    if (store("sortByRecent", v, sortByRecent()))
        Q_EMIT sortByRecentChanged();
}

int AppSettings::windowWidth() const { return m_settings.value("window/width", 1280).toInt(); }
void AppSettings::setWindowWidth(int v)
{
    v = qBound(320, v, 16384);
    if (store("window/width", v, windowWidth()))
        Q_EMIT windowWidthChanged();
}

int AppSettings::windowHeight() const { return m_settings.value("window/height", 820).toInt(); }
void AppSettings::setWindowHeight(int v)
{
    v = qBound(240, v, 16384);
    if (store("window/height", v, windowHeight()))
        Q_EMIT windowHeightChanged();
}

QString AppSettings::windowMode() const { return oneOf(m_settings.value("window/mode").toString(), kWindowModes); }
void AppSettings::setWindowMode(const QString &v)
{
    if (!kWindowModes.contains(v))
        return;
    if (store("window/mode", v, windowMode()))
        Q_EMIT windowModeChanged();
}

QVariantMap AppSettings::bookState(const QString &key) const
{
    return m_settings.value(QStringLiteral("books/") + key).toMap();
}

void AppSettings::setBookState(const QString &key, const QVariantMap &state)
{
    QVariantMap merged = bookState(key);
    for (auto it = state.begin(); it != state.end(); ++it)
        merged.insert(it.key(), it.value());
    m_settings.setValue(QStringLiteral("books/") + key, merged);
}

void AppSettings::removeBookState(const QString &key)
{
    m_settings.remove(QStringLiteral("books/") + key);
}

QString AppSettings::homePath() const
{
    return QDir::homePath();
}

bool AppSettings::ensureFolder(const QString &path) const
{
    return QDir().mkpath(path);
}
