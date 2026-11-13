// Spike 001 — can an unprivileged Qt app read Wi-Fi scan results out of
// NetworkManager over D-Bus, and does it see live updates without asking?
//
//   spike001              one scan, then print the table
//   spike001 --watch 20   then keep listening for 20 s and print what arrives

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QMetaType>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVariantMap>
#include <algorithm>
#include <cstdio>

namespace {

const QString kService  = QStringLiteral("org.freedesktop.NetworkManager");
const QString kRoot     = QStringLiteral("/org/freedesktop/NetworkManager");
const QString kDeviceIf = QStringLiteral("org.freedesktop.NetworkManager.Device");
const QString kWifiIf   = QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless");
const QString kApIf     = QStringLiteral("org.freedesktop.NetworkManager.AccessPoint");
const QString kPropsIf  = QStringLiteral("org.freedesktop.DBus.Properties");

// GetAll wraps every value in a QDBusVariant, so unwrap before reading it.
QVariant unwrap(QVariant value) {
    if (value.metaType().id() == qMetaTypeId<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    return value;
}

QVariantMap propertiesOf(const QString &path, const QString &iface) {
    QVariantMap result;
    QDBusInterface properties(kService, path, kPropsIf, QDBusConnection::systemBus());
    const QDBusReply<QVariantMap> reply = properties.call(QStringLiteral("GetAll"), iface);
    if (!reply.isValid())
        return result;
    const QVariantMap raw = reply.value();
    for (auto it = raw.cbegin(); it != raw.cend(); ++it)
        result.insert(it.key(), unwrap(it.value()));
    return result;
}

QString wifiDevicePath() {
    QDBusInterface manager(kService, kRoot, kService, QDBusConnection::systemBus());
    const QDBusReply<QList<QDBusObjectPath> > devices = manager.call(QStringLiteral("GetDevices"));
    if (!devices.isValid()) {
        std::fprintf(stderr, "GetDevices failed: %s\n", qPrintable(devices.error().message()));
        return QString();
    }
    const QList<QDBusObjectPath> paths = devices.value();
    for (const QDBusObjectPath &path : paths)
        if (propertiesOf(path.path(), kDeviceIf).value(QStringLiteral("DeviceType")).toUInt() == 2u)
            return path.path();
    return QString();
}

QList<QDBusObjectPath> accessPointPaths(const QString &device) {
    QDBusInterface wireless(kService, device, kWifiIf, QDBusConnection::systemBus());
    const QDBusReply<QList<QDBusObjectPath> > byMethod =
        wireless.call(QStringLiteral("GetAccessPoints"));
    if (byMethod.isValid())
        return byMethod.value();

    // The property is an 'ao', which arrives as a QDBusArgument.
    const QVariant raw = propertiesOf(device, kWifiIf).value(QStringLiteral("AccessPoints"));
    if (raw.canConvert<QList<QDBusObjectPath> >())
        return raw.value<QList<QDBusObjectPath> >();
    QList<QDBusObjectPath> paths;
    if (raw.metaType().id() == qMetaTypeId<QDBusArgument>())
        raw.value<QDBusArgument>() >> paths;
    return paths;
}

struct Ap {
    QString bssid;
    QString ssid;
    uint frequency = 0;
    uint maxBitrate = 0;
    uint flags = 0;
    uint wpaFlags = 0;
    uint rsnFlags = 0;
    int strength = -1;
    int lastSeen = -1;
};

QList<Ap> readAccessPoints(const QString &device) {
    QList<Ap> aps;
    const QList<QDBusObjectPath> paths = accessPointPaths(device);
    for (const QDBusObjectPath &path : paths) {
        const QVariantMap p = propertiesOf(path.path(), kApIf);
        Ap ap;
        ap.bssid = p.value(QStringLiteral("HwAddress")).toString();
        ap.ssid = QString::fromUtf8(p.value(QStringLiteral("Ssid")).toByteArray());
        ap.frequency = p.value(QStringLiteral("Frequency")).toUInt();
        ap.maxBitrate = p.value(QStringLiteral("MaxBitrate")).toUInt();
        ap.flags = p.value(QStringLiteral("Flags")).toUInt();
        ap.wpaFlags = p.value(QStringLiteral("WpaFlags")).toUInt();
        ap.rsnFlags = p.value(QStringLiteral("RsnFlags")).toUInt();
        ap.strength = p.value(QStringLiteral("Strength")).toInt();
        ap.lastSeen = p.value(QStringLiteral("LastSeen")).toInt();
        aps.append(ap);
    }
    return aps;
}

QString securityOf(const Ap &ap) {
    if (!(ap.flags & 0x1u))
        return QStringLiteral("open");
    QStringList parts;
    if (ap.wpaFlags)
        parts << QStringLiteral("wpa");
    if (ap.rsnFlags)
        parts << QStringLiteral("rsn");
    return parts.isEmpty() ? QStringLiteral("privacy") : parts.join(QLatin1Char('+'));
}

void printAccessPoints(QList<Ap> aps) {
    std::sort(aps.begin(), aps.end(), [](const Ap &a, const Ap &b) { return a.strength > b.strength; });
    std::printf("%-22s %-18s %-5s %-7s %-7s %-6s %-9s %s\n",
                "SSID", "BSSID", "band", "freq", "dBm*", "rate", "security", "LastSeen");
    for (const Ap &ap : aps) {
        const int band = ap.frequency >= 5925 ? 6 : (ap.frequency >= 5150 ? 5 : 2);
        std::printf("%-22s %-18s %-5d %-7u %-7.1f %-6u %-9s %d\n",
                    qPrintable(ap.ssid.left(22)), qPrintable(ap.bssid), band, ap.frequency,
                    ap.strength / 2.0 - 100.0, ap.maxBitrate, qPrintable(securityOf(ap)), ap.lastSeen);
    }
    std::printf("(%d access points; dBm* derived from NM Strength = 2*(dBm+100))\n", int(aps.size()));
}

class EventLog : public QObject {
    Q_OBJECT
public:
    explicit EventLog(QObject *parent = nullptr) : QObject(parent) { m_clock.start(); }
    qint64 now() const { return m_clock.elapsed(); }

private:
    QElapsedTimer m_clock;
};

class ApWatcher : public QObject {
    Q_OBJECT
public:
    ApWatcher(EventLog *log, QString path, QObject *parent = nullptr)
        : QObject(parent), m_log(log), m_path(std::move(path)) {}

public slots:
    void propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &) {
        Q_UNUSED(iface)
        QStringList bits;
        if (changed.contains(QStringLiteral("Strength")))
            bits << QStringLiteral("Strength=%1")
                        .arg(unwrap(changed.value(QStringLiteral("Strength"))).toInt());
        if (changed.contains(QStringLiteral("LastSeen")))
            bits << QStringLiteral("LastSeen=%1")
                        .arg(unwrap(changed.value(QStringLiteral("LastSeen"))).toLongLong());
        if (bits.isEmpty())
            return;
        std::printf("[%6lld ms] %s  %s\n", (long long)m_log->now(),
                    qPrintable(m_path.section(QLatin1Char('/'), -1)), qPrintable(bits.join(QLatin1Char(' '))));
    }

private:
    EventLog *m_log;
    QString m_path;
};

class DeviceWatcher : public QObject {
    Q_OBJECT
public:
    explicit DeviceWatcher(EventLog *log, QObject *parent = nullptr) : QObject(parent), m_log(log) {}

public slots:
    void added(const QDBusObjectPath &path) {
        std::printf("[%6lld ms] AccessPointAdded   %s\n", (long long)m_log->now(), qPrintable(path.path()));
    }
    void removed(const QDBusObjectPath &path) {
        std::printf("[%6lld ms] AccessPointRemoved %s\n", (long long)m_log->now(), qPrintable(path.path()));
    }

private:
    EventLog *m_log;
};

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    int watchSeconds = 0;
    const int watchIndex = args.indexOf(QStringLiteral("--watch"));
    if (watchIndex > 0 && watchIndex + 1 < args.size())
        watchSeconds = args.at(watchIndex + 1).toInt();

    // Proof the signal wiring works: with --rescan N, ask for one scan N
    // seconds into the listening phase. Silence then means "nothing was
    // pushed", not "nothing was connected".
    int rescanSeconds = 0;
    const int rescanIndex = args.indexOf(QStringLiteral("--rescan"));
    if (rescanIndex > 0 && rescanIndex + 1 < args.size())
        rescanSeconds = args.at(rescanIndex + 1).toInt();

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        std::fprintf(stderr, "no system bus\n");
        return 1;
    }

    const QString device = wifiDevicePath();
    if (device.isEmpty()) {
        std::fprintf(stderr, "NetworkManager reports no Wi-Fi device\n");
        return 1;
    }
    std::printf("wifi device: %s\n", qPrintable(device));

    // 1. Ask for a scan. No privilege, no polkit prompt.
    QDBusInterface wireless(kService, device, kWifiIf, bus);
    QElapsedTimer scanClock;
    scanClock.start();
    const QDBusReply<void> scan = wireless.call(QStringLiteral("RequestScan"), QVariantMap());
    const qint64 scanCallMs = scanClock.elapsed();
    if (scan.isValid()) {
        std::printf("RequestScan: accepted in %lld ms\n", (long long)scanCallMs);
    } else {
        std::printf("RequestScan: REFUSED in %lld ms — %s: %s\n", (long long)scanCallMs,
                    qPrintable(scan.error().name()), qPrintable(scan.error().message()));
    }

    QThread::msleep(6000);

    // 2. Read what the scan left behind.
    printAccessPoints(readAccessPoints(device));

    if (watchSeconds <= 0)
        return 0;

    // 3. Listen, asking for nothing, to see what NetworkManager pushes out.
    EventLog log;
    DeviceWatcher deviceWatcher(&log);
    bus.connect(kService, device, kWifiIf, QStringLiteral("AccessPointAdded"), &deviceWatcher,
                SLOT(added(QDBusObjectPath)));
    bus.connect(kService, device, kWifiIf, QStringLiteral("AccessPointRemoved"), &deviceWatcher,
                SLOT(removed(QDBusObjectPath)));

    QList<QObject *> watchers;
    const QList<QDBusObjectPath> paths = accessPointPaths(device);
    for (const QDBusObjectPath &path : paths) {
        ApWatcher *watcher = new ApWatcher(&log, path.path(), &app);
        bus.connect(kService, path.path(), kPropsIf, QStringLiteral("PropertiesChanged"), watcher,
                    SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
        watchers.append(watcher);
    }

    std::printf("\n--- listening %d s, requesting no scans ---\n", watchSeconds);
    if (rescanSeconds > 0) {
        std::printf("--- (one RequestScan at %d s, to prove the signal wiring) ---\n", rescanSeconds);
        QTimer::singleShot(rescanSeconds * 1000, &app, [&wireless, &log]() {
            const QDBusReply<void> again = wireless.call(QStringLiteral("RequestScan"), QVariantMap());
            std::printf("[%6lld ms] RequestScan %s\n", (long long)log.now(),
                        again.isValid() ? "accepted" : qPrintable(QStringLiteral("%1: %2")
                            .arg(again.error().name(), again.error().message())));
        });
    }
    QTimer::singleShot(watchSeconds * 1000, &app, &QCoreApplication::quit);
    app.exec();
    std::printf("--- done ---\n");

    qDeleteAll(watchers);
    return 0;
}

#include "main.moc"
