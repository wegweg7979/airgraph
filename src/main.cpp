// Airgraph — see which Wi-Fi channels are crowded, and how strong each AP is.

#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSharedPointer>
#include <QTimer>
#include <QUrl>

#include "apmodel.h"
#include "nmwifisource.h"
#include "scanbackend.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("airgraph");
    app.setApplicationVersion("0.1.0");

    // Matches the window to airgraph.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("airgraph");
    app.setWindowIcon(QIcon::fromTheme("airgraph"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    NmWifiSource source;
    ScanBackend backend(&source);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // `airgraph --grab <png>` renders the real window offscreen and saves what
    // it looks like, so the README screenshot and any visual check need no
    // display and no desktop session. It waits for the first scan to actually
    // finish (bounded) rather than catching the window mid-scan.
    const QStringList arguments = app.arguments();
    const int grabIndex = arguments.indexOf(QStringLiteral("--grab"));
    if (grabIndex > 0 && grabIndex + 1 < arguments.size()) {
        const QString path = arguments.at(grabIndex + 1);
        auto *settle = new QTimer(&app);
        auto waited = QSharedPointer<int>::create(0);
        settle->setInterval(500);
        QObject::connect(settle, &QTimer::timeout, &app, [&engine, &backend, path, settle, waited]() {
            *waited += 500;
            // Two seconds of settled rendering, or 20 s of waiting, whichever
            // comes first: never a screenshot of a scan still in flight.
            if (backend.waitingForScan() && *waited < 20000)
                return;
            if (*waited < 2000)
                return;
            settle->stop();

            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
            if (!window) {
                qWarning("airgraph: no window to grab");
                QCoreApplication::exit(1);
                return;
            }
            const QImage shot = window->grabWindow();
            const bool saved = shot.save(path);
            qInfo() << "airgraph: grabbed" << shot.size() << "after" << *waited << "ms, saved:" << saved;
            QCoreApplication::exit(saved ? 0 : 1);
        });
        settle->start();
    }

    return app.exec();
}
