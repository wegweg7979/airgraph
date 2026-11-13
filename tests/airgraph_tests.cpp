#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "apfiltermodel.h"
#include "apmodel.h"
#include "bandplan.h"
#include "scanbackend.h"
#include "spectrum.h"
#include "theme.h"
#include "translator.h"
#include "wifisource.h"

// The scan backend's job is cadence and merging, not D-Bus, so it is tested
// against a source with no bus behind it.
class FakeWifiSource : public WifiSource {
public:
    explicit FakeWifiSource(QObject *parent = nullptr) : WifiSource(parent) {}

    bool available() const override { return availableValue; }
    QString problem() const override { return problemValue; }
    QString device() const override { return QStringLiteral("fake0"); }
    QList<AccessPoint> accessPoints() override {
        if (completedOnRead)
            scanCompleted = true;
        return points;
    }
    bool requestScan(QString *error) override {
        ++scanRequests;
        if (!scanAccepted) {
            if (error)
                *error = QStringLiteral("org.freedesktop.NetworkManager.Device.NotAllowed: refused");
            return false;
        }
        if (scanCompletes)
            scanCompleted = true;
        return true;
    }
    bool takeScanCompleted() override {
        const bool completed = scanCompleted;
        scanCompleted = false;
        return completed;
    }

    bool availableValue = true;
    bool scanAccepted = true;
    bool scanCompletes = true;
    bool scanCompleted = false;
    bool completedOnRead = false;
    QString problemValue;
    int scanRequests = 0;
    QList<AccessPoint> points;
};

static AccessPoint makeAp(const QString &ssid, const QString &bssid, int frequency, int signal,
                          bool connected = false, const QString &security = QStringLiteral("rsn")) {
    AccessPoint ap;
    ap.ssid = ssid;
    ap.bssid = bssid;
    ap.frequency = frequency;
    ap.signal = signal;
    ap.maxBitrate = 540000;
    ap.security = security;
    ap.connected = connected;
    return ap;
}

class AirgraphTests : public QObject {
    Q_OBJECT

private slots:
    // --- the derived values every view shares ---------------------------------

    void bandComesFromFrequency()
    {
        QCOMPARE(bandOf(2412), 2);
        QCOMPARE(bandOf(2462), 2);
        QCOMPARE(bandOf(5180), 5);
        QCOMPARE(bandOf(5500), 5);
        QCOMPARE(bandOf(5955), 6);
        QCOMPARE(bandOf(7115), 6);
    }

    void channelComesFromFrequency()
    {
        QCOMPARE(channelOf(2412), 1);
        QCOMPARE(channelOf(2462), 11);
        QCOMPARE(channelOf(2484), 14);
        QCOMPARE(channelOf(5180), 36);
        QCOMPARE(channelOf(5500), 100);
        QCOMPARE(channelOf(5620), 124);
        QCOMPARE(channelOf(5955), 1);
        QCOMPARE(channelOf(6135), 37);
        QCOMPARE(channelOf(100), -1);
    }

    void networkManagerStrengthBecomesDbm()
    {
        // NetworkManager's own mapping is strength = 2 * (dBm + 100).
        QCOMPARE(dbmOf(100), -50.0);
        QCOMPARE(dbmOf(70), -65.0);
        QCOMPARE(dbmOf(0), -100.0);
    }

    void widthIsAnAssumptionThatIsLabelled()
    {
        QCOMPARE(assumedWidthOf(2412), 20);
        QCOMPARE(assumedWidthOf(5180), 20);
    }

    void securityIsReadFromTheFlags()
    {
        QCOMPARE(securityOf(0, 0, 0), QStringLiteral("open"));
        QCOMPARE(securityOf(1, 0, 1), QStringLiteral("rsn"));
        QCOMPARE(securityOf(1, 1, 1), QStringLiteral("wpa+rsn"));
        QCOMPARE(securityOf(1, 0, 0), QStringLiteral("privacy"));
    }

    // --- the model -------------------------------------------------------------

    void modelInsertsUpdatesAndRemoves()
    {
        ApModel model;
        QSignalSpy countSpy(&model, &ApModel::countChanged);

        model.apply({ makeAp("alpha", "AA:AA:AA:AA:AA:01", 2412, 40),
                      makeAp("bravo", "AA:AA:AA:AA:AA:02", 5180, 80) });
        QCOMPARE(model.count(), 2);
        QCOMPARE(countSpy.count(), 1);

        // Strongest first: bravo at 80 outranks alpha at 40.
        QCOMPARE(model.data(model.index(0, 0), ApModel::SsidRole).toString(), QStringLiteral("bravo"));

        // A known BSSID is updated in place, keeping its first-seen time.
        const int firstSeenBefore = model.data(model.index(1, 0), ApModel::FirstSeenRole).toInt();
        model.apply({ makeAp("alpha", "AA:AA:AA:AA:AA:01", 2412, 90),
                      makeAp("bravo", "AA:AA:AA:AA:AA:02", 5180, 80) });
        QCOMPARE(model.count(), 2);
        QCOMPARE(countSpy.count(), 1);
        QCOMPARE(model.data(model.index(0, 0), ApModel::SsidRole).toString(), QStringLiteral("alpha"));
        QCOMPARE(model.data(model.index(1, 0), ApModel::FirstSeenRole).toInt(), firstSeenBefore);

        // An AP that stops answering leaves the table.
        model.apply({ makeAp("bravo", "AA:AA:AA:AA:AA:02", 5180, 80) });
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.data(model.index(0, 0), ApModel::SsidRole).toString(), QStringLiteral("bravo"));
        QCOMPARE(countSpy.count(), 2);
    }

    void modelPutsTheConnectedApFirst()
    {
        ApModel model;
        model.apply({ makeAp("far", "AA:AA:AA:AA:AA:01", 2412, 95),
                      makeAp("mine", "AA:AA:AA:AA:AA:02", 5180, 30, true) });
        QCOMPARE(model.data(model.index(0, 0), ApModel::SsidRole).toString(), QStringLiteral("mine"));
        QVERIFY(model.data(model.index(0, 0), ApModel::ConnectedRole).toBool());
        QVERIFY(!model.data(model.index(1, 0), ApModel::ConnectedRole).toBool());
    }

    void modelCarriesWhatTheViewsNeed()
    {
        ApModel model;
        model.apply({ makeAp("alpha", "AA:AA:AA:AA:AA:01", 5500, 70, false, QStringLiteral("open")) });
        const QModelIndex row = model.index(0, 0);
        QCOMPARE(model.data(row, ApModel::BandRole).toInt(), 5);
        QCOMPARE(model.data(row, ApModel::ChannelRole).toInt(), 100);
        QCOMPARE(model.data(row, ApModel::WidthRole).toInt(), 20);
        QCOMPARE(model.data(row, ApModel::DbmRole).toDouble(), -65.0);
        QCOMPARE(model.data(row, ApModel::SignalRole).toInt(), 70);
        QCOMPARE(model.data(row, ApModel::SecurityRole).toString(), QStringLiteral("open"));
        QCOMPARE(model.data(row, ApModel::RateRole).toUInt(), 540000u);
        QCOMPARE(model.data(row, ApModel::BssidRole).toString(), QStringLiteral("AA:AA:AA:AA:AA:01"));
    }

    void roleNamesMatchTheQml()
    {
        ApModel model;
        const QHash<int, QByteArray> roles = model.roleNames();
        QCOMPARE(roles.value(ApModel::SsidRole), QByteArray("ssid"));
        QCOMPARE(roles.value(ApModel::DbmRole), QByteArray("dbm"));
        QCOMPARE(roles.value(ApModel::ConnectedRole), QByteArray("connected"));
        QCOMPARE(roles.value(ApModel::ChangedRole), QByteArray("changedSeconds"));
    }

    // --- the backend -----------------------------------------------------------

    void backendShowsWhatTheSourceReports()
    {
        FakeWifiSource source;
        source.points = { makeAp("alpha", "AA:AA:AA:AA:AA:01", 2412, 40) };
        ScanBackend backend(&source);
        QCOMPARE(backend.accessPoints()->count(), 1);
        QVERIFY(backend.available());
        QCOMPARE(backend.device(), QStringLiteral("fake0"));
        QCOMPARE(backend.scanIntervalSeconds(), 10);
    }

    void backendReportsABlockerInsteadOfPretending()
    {
        FakeWifiSource source;
        source.availableValue = false;
        source.problemValue = QStringLiteral("Wi-Fi is switched off.");
        ScanBackend backend(&source);

        QVERIFY(!backend.available());
        QCOMPARE(backend.problem(), QStringLiteral("Wi-Fi is switched off."));
        QCOMPARE(backend.accessPoints()->count(), 0);
    }

    void backendRefreshesWhenTheSourceSaysSomethingMoved()
    {
        FakeWifiSource source;
        source.points = { makeAp("alpha", "AA:AA:AA:AA:AA:01", 2412, 40) };
        ScanBackend backend(&source);

        source.points.append(makeAp("bravo", "AA:AA:AA:AA:AA:02", 5180, 60));
        emit source.changed();
        QCOMPARE(backend.accessPoints()->count(), 2);
    }

    void backendAsksForAScanAndWaitsForItToReallyHappen()
    {
        FakeWifiSource source;
        source.points = { makeAp("alpha", "AA:AA:AA:AA:AA:01", 2412, 40) };
        ScanBackend backend(&source);

        // The constructor asks for a scan, so the window starts out waiting for
        // it: "scanning…" is the honest opening state, not a stale table.
        QVERIFY(backend.waitingForScan());
        QCOMPARE(source.scanRequests, 1);
        source.completedOnRead = true;
        backend.refreshNow();
        QVERIFY(!backend.waitingForScan());

        // Now a request that is accepted but silently coalesced away: the
        // window must not claim fresh data (spike 003).
        source.completedOnRead = false;
        source.scanCompletes = false;
        backend.rescan();
        QCOMPARE(source.scanRequests, 2);
        QVERIFY(backend.waitingForScan());
        QTest::qWait(50);
        backend.refreshNow();
        QVERIFY(backend.waitingForScan());

        // And then one that really ran.
        source.completedOnRead = true;
        backend.rescan();
        backend.refreshNow();
        QVERIFY(!backend.waitingForScan());
    }

    void backendSurfacesARefusedScan()
    {
        FakeWifiSource source;
        source.scanAccepted = false;
        ScanBackend backend(&source);
        backend.rescan();
        QVERIFY(!backend.scanError().isEmpty());
        QVERIFY(backend.scanError().contains(QStringLiteral("refused")));

        source.scanAccepted = true;
        backend.rescan();
        QVERIFY(backend.scanError().isEmpty());
    }

    void backendAgeCountsUpAndResetsOnACompletedScan()
    {
        FakeWifiSource source;
        source.points = { makeAp("alpha", "AA:AA:AA:AA:AA:01", 2412, 40) };
        ScanBackend backend(&source);
        QCOMPARE(backend.secondsSinceScan(), 0);

        QTest::qWait(1100);
        QVERIFY(backend.secondsSinceScan() >= 1);

        source.completedOnRead = true;
        backend.refreshNow();
        QCOMPARE(backend.secondsSinceScan(), 0);
    }

    // --- theme -----------------------------------------------------------------

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QStringLiteral("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QStringLiteral("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QStringLiteral("#112233"));
        QCOMPARE(theme.accentForeground(), QStringLiteral("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    // --- translator ------------------------------------------------------------

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QStringLiteral("en"));
        QVERIFY(translator.translate("Main", "R").isNull());
    }

    void translatorCountsAccessPoints()
    {
        QTemporaryDir dir;
        write(dir.path() + "/en.json", R"json({"%n access point(s)": {"one": "%n access point", "other": "%n access points"}})json");
        Translator translator(dir.path() + "/language", dir.path());
        QCoreApplication::installTranslator(&translator);

        // Through Qt's own translate(), which is what qsTr() calls and what
        // substitutes %n — the translator only picks the plural form.
        QCOMPARE(QCoreApplication::translate("Main", "%n access point(s)", nullptr, 1),
                 QStringLiteral("1 access point"));
        QCOMPARE(QCoreApplication::translate("Main", "%n access point(s)", nullptr, 7),
                 QStringLiteral("7 access points"));

        QCoreApplication::removeTranslator(&translator);
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Scanning…": "A procurar…", "R": "R"})");
        write(dir.path() + "/pt-BR.json", R"({"Scanning…": "Procurando…"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QStringLiteral("pt-BR"));
        QCOMPARE(translator.translate("Main", "Scanning…"), QStringLiteral("Procurando…"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    // --- the spectrum arithmetic ----------------------------------------------

    void bandForCarriesTheRadiosChannels()
    {
        const spectrum::Band band = spectrum::bandFor(2, BandPlan::standard(2));
        QCOMPARE(band.label(), QStringLiteral("2.4 GHz"));
        QCOMPARE(band.tickFrequencies.size(), 14);
        // A little air either side, so the end curves are not clipped.
        QVERIFY(band.minFrequency < 2412);
        QVERIFY(band.maxFrequency > 2484);
        QCOMPARE(spectrum::bandFor(6, BandPlan::standard(6)).label(), QStringLiteral("6 GHz"));
    }

    void frequencyAxisIsLinearInMhz()
    {
        const spectrum::Band band = spectrum::bandFor(2, BandPlan::standard(2));
        const QRectF plot(0, 0, 100, 50);

        QCOMPARE(spectrum::frequencyToX(band.minFrequency, band, plot), 0.0);
        QCOMPARE(spectrum::frequencyToX(band.maxFrequency, band, plot), 100.0);
        // 2412 and 2437 are five channels apart, so they must land in order and
        // stay inside the plot.
        const double first = spectrum::frequencyToX(2412, band, plot);
        const double sixth = spectrum::frequencyToX(2437, band, plot);
        QVERIFY(sixth > first);
        QVERIFY(first > plot.left() && sixth < plot.right());
    }

    void dbmAxisGrowsUpward()
    {
        const QRectF plot(0, 0, 100, 200);
        QCOMPARE(spectrum::dbmToY(-20.0, plot), 0.0);
        QCOMPARE(spectrum::dbmToY(-100.0, plot), 200.0);
        QVERIFY(spectrum::dbmToY(-60.0, plot) < spectrum::dbmToY(-80.0, plot));
        // Below the floor is clamped, not drawn off the bottom.
        QCOMPARE(spectrum::dbmToY(-120.0, plot), 200.0);
    }

    void bellIsFlatOnTopAndFloorAtTheEdges()
    {
        const spectrum::Band band = spectrum::bandFor(5, BandPlan::standard(5));
        const QRectF plot(0, 0, 400, 200);
        const QList<QPointF> narrow = spectrum::bellCurve(5500, 20, -60.0, band, plot);
        const QList<QPointF> wide = spectrum::bellCurve(5500, 40, -60.0, band, plot);

        QVERIFY(narrow.size() > 8);
        // Wider channel, wider bell, same peak height.
        QVERIFY(wide.last().x() - wide.first().x() > narrow.last().x() - narrow.first().x());
        const double peak = spectrum::dbmToY(-60.0, plot);
        QCOMPARE(wide.at(wide.size() / 2).y(), peak);
        // The ends have fallen to the floor.
        QCOMPARE(wide.first().y(), spectrum::dbmToY(-80.0, plot));
        QCOMPARE(narrow.first().y(), spectrum::dbmToY(-80.0, plot));
    }

    void congestionCountsOverlappingStrength()
    {
        const QList<AccessPoint> empty;
        QCOMPARE(spectrum::channelScore(empty, 2412), 0.0);
        QCOMPARE(spectrum::starsFor(0.0), 5);

        const QList<AccessPoint> one = { makeAp("a", "AA:AA:AA:AA:AA:01", 2412, 70) };
        const double single = spectrum::channelScore(one, 2412);
        QVERIFY(single > 0.0 && single < 1.0);

        // Two strong APs on the same channel are worse than one.
        QList<AccessPoint> two = one;
        two.append(makeAp("b", "AA:AA:AA:AA:AA:02", 2412, 100));
        QVERIFY(spectrum::channelScore(two, 2412) > single);

        // A channel two channels away is clear of it.
        QCOMPARE(spectrum::channelScore(one, 2437), 0.0);
        QCOMPARE(spectrum::starsFor(2.0), 1);
    }

    void bestChannelAvoidsTheCrowd()
    {
        const int one = 2412, six = 2437, eleven = 2462;
        const QList<AccessPoint> busy = { makeAp("a", "AA:AA:AA:AA:AA:01", 2437, 100),
                                          makeAp("b", "AA:AA:AA:AA:AA:02", 2437, 90) };
        QCOMPARE(spectrum::bestChannel(busy, { one, six, eleven }), one);

        QCOMPARE(spectrum::bestChannel({}, { one, six, eleven }), one);
    }

    void strengthColorIsGreenAmberRed()
    {
        QCOMPARE(spectrum::strengthColor(-50.0), QColor(0x7f, 0xd1, 0x8a));
        QCOMPARE(spectrum::strengthColor(-70.0), QColor(0xe6, 0xc0, 0x6a));
        QCOMPARE(spectrum::strengthColor(-90.0), QColor(0xe0, 0x7a, 0x7a));
    }

    // --- the band plan, against real iw output --------------------------------

    void bandPlanReadsThePhysChannels()
    {
        // Trimmed from `iw phy phy0 info` on this machine.
        const QString phy = QStringLiteral(
            "Wiphy phy0\n"
            "\tBand 1:\n"
            "\t\tFrequencies:\n"
            "\t\t\t* 2412.0 MHz [1] (22.0 dBm)\n"
            "\t\t\t* 2417.0 MHz [2] (22.0 dBm)\n"
            "\t\t\t* 2484.0 MHz [14] (22.0 dBm)\n"
            "\tBand 2:\n"
            "\t\t\t* 5180.0 MHz [36] (22.0 dBm)\n"
            "\tBand 4:\n"
            "\t\t\t* 5955.0 MHz [1] (22.0 dBm) (no IR)\n"
            "\t\t\t* 5975.0 MHz [5] (22.0 dBm) (no IR)\n");

        const QHash<int, QList<int> > parsed = BandPlan::parse(phy);
        QCOMPARE(parsed.value(2), QList<int>({ 2412, 2417, 2484 }));
        QCOMPARE(parsed.value(5), QList<int>({ 5180 }));
        QCOMPARE(parsed.value(6), QList<int>({ 5955, 5975 }));
    }

    void bandPlanFallsBackToTheStandardTable()
    {
        BandPlan plan;
        QCOMPARE(plan.source(), QStringLiteral("standard channel table"));
        QCOMPARE(plan.frequencies(2).size(), 14);
        QVERIFY(plan.frequencies(5).contains(5180));
        QVERIFY(plan.frequencies(5).contains(5500));
        QVERIFY(plan.frequencies(6).contains(5955));
        QVERIFY(plan.advertised(6));

        // An empty parse must not shrink the plan to nothing.
        QVERIFY(BandPlan::parse(QStringLiteral("garbage")).isEmpty());
    }

    void linkWidthComesFromTheDriver()
    {
        // `iw dev wlp4s0 info` on this machine, verbatim.
        const QString info = QStringLiteral(
            "Interface wlp4s0\n"
            "\tifindex 4\n"
            "\twdev 0x1\n"
            "\taddr 0e:7b:d5:71:f7:9c\n"
            "\tssid hurunagus\n"
            "\ttype managed\n"
            "\twiphy 0\n"
            "\tchannel 100 (5500 MHz), width: 40 MHz, center1: 5510 MHz\n"
            "\ttxpower 22.00 dBm\n");

        QCOMPARE(BandPlan::parseLinkWidth(info), 40);
        QCOMPARE(BandPlan::parseLinkFrequency(info), 5500);
        QCOMPARE(BandPlan::parseLinkCenterFrequency(info), 5510);
        QCOMPARE(BandPlan::parseLinkSsid(info), QStringLiteral("hurunagus"));
        QCOMPARE(BandPlan::parseLinkWidth(QStringLiteral("no link here")), 0);
    }

    void widthIsMeasuredOrAssumed()
    {
        AccessPoint unknown = makeAp("a", "AA:AA:AA:AA:AA:01", 5500, 70);
        QCOMPARE(widthOf(unknown), 20);
        QVERIFY(widthIsAssumed(unknown));

        AccessPoint measured = unknown;
        measured.widthMhz = 40;
        QCOMPARE(widthOf(measured), 40);
        QVERIFY(!widthIsAssumed(measured));
    }

    void modelWidthRoleFollowsTheMeasurement()
    {
        ApModel model;
        AccessPoint measured = makeAp("mine", "AA:AA:AA:AA:AA:01", 5500, 70, true);
        measured.widthMhz = 40;
        model.apply({ measured, makeAp("other", "AA:AA:AA:AA:AA:02", 5500, 60) });

        QCOMPARE(model.data(model.index(0, 0), ApModel::WidthRole).toInt(), 40);
        QVERIFY(!model.data(model.index(0, 0), ApModel::WidthAssumedRole).toBool());
        QCOMPARE(model.data(model.index(1, 0), ApModel::WidthRole).toInt(), 20);
        QVERIFY(model.data(model.index(1, 0), ApModel::WidthAssumedRole).toBool());
    }

    // --- bands in the UI: counts and the table filter -------------------------

    void countsPerBand()
    {
        ApModel model;
        model.apply({ makeAp("a", "AA:AA:AA:AA:AA:01", 2412, 70),
                      makeAp("b", "AA:AA:AA:AA:AA:02", 2462, 60),
                      makeAp("c", "AA:AA:AA:AA:AA:03", 5180, 80) });
        const QVariantMap counts = model.countsByBand();
        QCOMPARE(counts.value("2").toInt(), 2);
        QCOMPARE(counts.value("5").toInt(), 1);
        QCOMPARE(counts.value("6").toInt(), 0);
    }

    void tableShowsOneBandAtATime()
    {
        ApModel model;
        model.apply({ makeAp("a", "AA:AA:AA:AA:AA:01", 2412, 70),
                      makeAp("b", "AA:AA:AA:AA:AA:02", 2462, 60),
                      makeAp("c", "AA:AA:AA:AA:AA:03", 5180, 80) });

        ApFilterModel filtered;
        filtered.setSourceModel(&model);
        QCOMPARE(filtered.count(), 2);          // 2.4 GHz by default

        filtered.setGigahertz(5);
        QCOMPARE(filtered.count(), 1);
        QCOMPARE(filtered.index(0, 0).data(ApModel::SsidRole).toString(), QStringLiteral("c"));

        filtered.setGigahertz(6);
        QCOMPARE(filtered.count(), 0);
    }

    void backendOpensOnTheBandInUse()
    {
        FakeWifiSource source;
        source.points = { makeAp("2g", "AA:AA:AA:AA:AA:01", 2412, 70),
                          makeAp("5g", "AA:AA:AA:AA:AA:02", 5500, 60, true) };
        ScanBackend backend(&source);
        // Associated on 5 GHz, so that is the band that opens.
        QCOMPARE(backend.gigahertz(), 5);
        QCOMPARE(backend.bandAccessPoints()->count(), 1);

        backend.setGigahertz(2);
        QCOMPARE(backend.gigahertz(), 2);
        QCOMPARE(backend.bandAccessPoints()->count(), 1);
    }

private:
    static bool write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write(contents) == contents.size();
    }
};

QTEST_MAIN(AirgraphTests)
#include "airgraph_tests.moc"
