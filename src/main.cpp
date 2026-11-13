// Airgraph — see which Wi-Fi channels are crowded, and how strong each AP is.

#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSharedPointer>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QEventLoop>

#include <cstdio>
#include <memory>

#include "apmodel.h"
#include "bandplan.h"
#include "cli.h"
#include "historyitem.h"
#include "nmwifisource.h"
#include "scanbackend.h"
#include "sessionexport.h"
#include "spectrumitem.h"
#include "theme.h"
#include "translator.h"

namespace {

// A source that never scans: --demo fills the model with fabricated access
// points, and without this the live scan would overwrite them within seconds and
// a "demo" screenshot would show the real neighbours.
class DemoWifiSource : public WifiSource {
public:
    explicit DemoWifiSource(QObject *parent = nullptr) : WifiSource(parent) {}

    bool available() const override { return false; }
    QString problem() const override { return QString(); }   // nothing wrong: there is simply no radio
    QString device() const override { return QStringLiteral("demo — fabricated access points"); }
    QList<AccessPoint> accessPoints() override { return {}; }
    bool requestScan(QString *) override { return true; }
    bool takeScanCompleted() override { return false; }
};

// The scan is asynchronous: NetworkManager accepts the request instantly and the
// results arrive seconds later. Waiting for the completion is what makes a
// headless command honest about what it printed.
bool waitForScan(ScanBackend &backend, int timeoutMs) {
    if (!backend.waitingForScan())
        return true;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    const QMetaObject::Connection finished =
        QObject::connect(&backend, &ScanBackend::waitingForScanChanged, &loop, [&backend, &loop]() {
            if (!backend.waitingForScan())
                loop.quit();
        });
    timeout.start(timeoutMs);
    loop.exec();
    QObject::disconnect(finished);
    return !backend.waitingForScan();
}

void print(const QString &text) {
    QTextStream out(stdout, QIODevice::WriteOnly);
    out << text;
    if (!text.endsWith(QLatin1Char('\n')))
        out << '\n';
    out.flush();
}

// --json is asked for in any position; path is the first bare argument after
// the command.
bool wantsJson(const QStringList &arguments) {
    return arguments.contains(QStringLiteral("--json"));
}

QString argumentValue(const QStringList &arguments, const QString &name, const QString &fallback = QString()) {
    const int index = arguments.indexOf(name);
    if (index > 0 && index + 1 < arguments.size())
        return arguments.at(index + 1);
    return fallback;
}

// scan / check / export: no window, exit 0 or 1, errors on stderr.
int runHeadless(const QStringList &arguments, ScanBackend &backend, NmWifiSource &source,
                BandPlan &bandPlan) {
    const QString command = arguments.at(1);

    if (command == QStringLiteral("help")) {
        print(cli::usage());
        return 0;
    }

    if (command == QStringLiteral("check")) {
        cli::Check check;
        check.device = source.device();
        check.problem = source.problem();
        check.available = source.available();
        check.phySource = bandPlan.source();
        for (int gigahertz : { 2, 5, 6 })
            check.bandChannels.insert(gigahertz, bandPlan.count(gigahertz));

        if (check.available) {
            QString error;
            check.scanAccepted = source.requestScan(&error);
            if (!check.scanAccepted && check.problem.isEmpty())
                check.problem = error;
            waitForScan(backend, 20000);
            check.accessPoints = backend.accessPoints()->count();
        }

        print(wantsJson(arguments) ? cli::checkJson(check) : cli::checkText(check));
        return check.available ? 0 : 1;
    }

    if (command == QStringLiteral("scan")) {
        if (!source.available()) {
            std::fprintf(stderr, "airgraph: cannot scan: %s\n", qPrintable(source.problem()));
            return 1;
        }
        backend.rescan();
        if (!waitForScan(backend, 20000)) {
            std::fprintf(stderr, "airgraph: no scan completed within 20 s (NetworkManager coalesces requests)\n");
            return 1;
        }
        const QList<AccessPoint> accessPoints = backend.accessPoints()->accessPoints();
        if (wantsJson(arguments)) {
            // A snapshot, not a session: `export` is what carries the series.
            const ApHistory none;
            print(sessionexport::toJson(accessPoints, none, source.device(), false));
        } else {
            print(cli::scanText(accessPoints, source.device()));
        }
        return accessPoints.isEmpty() ? 1 : 0;
    }

    // export --csv|--json [path] [--live seconds]
    const bool json = wantsJson(arguments);
    if (source.available()) {
        backend.rescan();
        waitForScan(backend, 20000);
        const int liveSeconds = argumentValue(arguments, QStringLiteral("--live"), QStringLiteral("0")).toInt();
        if (liveSeconds > 0) {
            QEventLoop loop;
            QTimer::singleShot(liveSeconds * 1000, &loop, &QEventLoop::quit);
            loop.exec();
        }
    }

    // One writer, the one that was asked for.
    const QString path = json ? backend.exportSessionJson() : backend.exportSessionCsv();

    if (path.isEmpty()) {
        std::fprintf(stderr, "airgraph: could not write the session to %s\n",
                     qPrintable(backend.exportDirectory()));
        return 1;
    }
    print(path);
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    // A command that opens no window must set the platform before any GUI object
    // exists, which is why this is read straight from argv.
    QStringList arguments;
    for (int i = 0; i < argc; ++i)
        arguments.append(QString::fromLocal8Bit(argv[i]));
    const bool headless = cli::isHeadlessCommand(arguments);
    if (headless)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("airgraph");
    app.setApplicationVersion("0.1.1");

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

    // --demo is parsed before the backend exists, because the source it uses is
    // decided then: fabricated data must not be fighting a live scan.
    const bool demo = arguments.contains(QStringLiteral("--demo"));
    NmWifiSource source;
    std::unique_ptr<DemoWifiSource> demoSource;
    WifiSource *wifiSource = &source;
    if (demo) {
        demoSource = std::make_unique<DemoWifiSource>();
        wifiSource = demoSource.get();
    }
    ScanBackend backend(wifiSource);

    // Which channels this radio actually has, straight from the phy: `iw phy`
    // needs no privilege, unlike scanning. Even in demo mode the axis is this
    // machine's real channel plan.
    BandPlan bandPlan;
    bandPlan.load(source.device());

    if (headless) {
        if (arguments.at(1) == QStringLiteral("export"))
            backend.setExportDirectory(argumentValue(arguments, QStringLiteral("--out"), backend.exportDirectory()));
        return runHeadless(app.arguments(), backend, source, bandPlan);
    }

    if (demo)
        backend.applyDemoData();

    qmlRegisterType<SpectrumItem>("Airgraph", 1, 0, "SpectrumView");
    qmlRegisterType<HistoryItem>("Airgraph", 1, 0, "HistoryView");

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("bandPlan", &bandPlan);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // `airgraph --grab <png>` renders the real window offscreen and saves what
    // it looks like, so the README screenshot and any visual check need no
    // display and no desktop session. It waits for the first scan to actually
    // finish (bounded) rather than catching the window mid-scan.
    // `--band <2|5|6>` picks the band first, for a screenshot of a chosen one.
    const QStringList uiArguments = app.arguments();
    const int bandIndex = uiArguments.indexOf(QStringLiteral("--band"));
    if (bandIndex > 0 && bandIndex + 1 < uiArguments.size())
        backend.setGigahertz(uiArguments.at(bandIndex + 1).toInt());

    const int grabIndex = uiArguments.indexOf(QStringLiteral("--grab"));
    if (grabIndex > 0 && grabIndex + 1 < uiArguments.size()) {
        const QString path = uiArguments.at(grabIndex + 1);

        // --settle N holds the window open for at least N seconds before the
        // grab, so a screenshot can show history rather than one scan.
        int settleSeconds = 2;
        const int settleIndex = uiArguments.indexOf(QStringLiteral("--settle"));
        if (settleIndex > 0 && settleIndex + 1 < uiArguments.size())
            settleSeconds = uiArguments.at(settleIndex + 1).toInt();

        auto *settle = new QTimer(&app);
        auto waited = QSharedPointer<int>::create(0);
        settle->setInterval(500);
        QObject::connect(settle, &QTimer::timeout, &app,
                         [&engine, &backend, path, settle, waited, settleSeconds]() {
            *waited += 500;
            // Never a screenshot of a scan still in flight.
            if (backend.waitingForScan() && *waited < 20000)
                return;
            if (*waited < settleSeconds * 1000)
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
