#include "appsettings.h"
#include "librarymodel.h"
#include "reader.h"
#include "voicemodel.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("surfacego-reader"));
    QGuiApplication::setApplicationName(QStringLiteral("surfacego-reader"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("SurfaceGo Reader"));
    QGuiApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("surfacego-reader"));
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("surfacego-reader"),
                                                    QIcon::fromTheme(QStringLiteral("accessories-ebook-reader"))));

    // Material has large, touch-sized controls (the KDE desktop style does not).
    QQuickStyle::setStyle(QStringLiteral("Material"));

    AppSettings settings;
    VoiceModel voices(&settings);
    LibraryModel library(&settings);
    Reader reader(&settings, &voices);

    QObject::connect(&reader, &Reader::bookClosed, &library, &LibraryModel::updateProgress);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &reader, &Reader::saveState);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("library"), &library);
    engine.rootContext()->setContextProperty(QStringLiteral("voiceList"), &voices);
    engine.rootContext()->setContextProperty(QStringLiteral("reader"), &reader);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("SGReader", "Main");

    // Open a file passed on the command line (e.g. from the file manager).
    const QStringList args = app.arguments();
    if (args.size() > 1)
        reader.openBook(args.at(1));

    return app.exec();
}
