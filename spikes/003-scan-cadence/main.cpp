// Spike 003 — how often can we ask NetworkManager to scan before it refuses,
// and do strengths move on their own between scans?
//
//   spike003 <interval-seconds> <attempts> [passive-seconds]

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
#include <QThread>
#include <QTimer>
#include <QVariantMap>
#include <cstdio>

namespace {

const QString kService  = QStringLiteral("org.freedesktop.NetworkManager");
const QString kRoot     = QStringLiteral("/org/freedesktop/NetworkManager");
const QString kDeviceIf = QStringLiteral("org.freedesktop.NetworkManager.Device");
const QString kWifiIf   = QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless");
const QString kApIf     = QStringLiteral("org.freedesktop.NetworkManager.AccessPoint");
const QString kPropsIf  = QStringLiteral("org.freedesktop.DBus.Properties");

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
    if (!devices.isValid())
        return QString();
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
    const QVariant raw = propertiesOf(device, kWifiIf).value(QStringLiteral("AccessPoints"));
    QList<QDBusObjectPath> paths;
    if (raw.metaType().id() == qMetaTypeId<QDBusArgument>())
        raw.value<QDBusArgument>() >> paths;
    return paths;
}

// Strongest strength on the air right now, plus the AP count, so an attempt
// that changed nothing is visible as an unchanged pair.
QString airSummary(const QString &device) {
    const QList<QDBusObjectPath> paths = accessPointPaths(device);
    int best = -1;
    for (const QDBusObjectPath &path : paths) {
        const int strength = propertiesOf(path.path(), kApIf).value(QStringLiteral("Strength")).toInt();
        if (strength > best)
            best = strength;
    }
    return QStringLiteral("%1 APs, strongest %2 dBm*")
        .arg(paths.size())
        .arg(best < 0 ? 0.0 : best / 2.0 - 100.0, 0, 'f', 1);
}

class StrengthCounter : public QObject {
    Q_OBJECT
public:
    explicit StrengthCounter(QObject *parent = nullptr) : QObject(parent) { m_clock.start(); }
    int updates() const { return m_updates; }
    int paths() const { return m_paths.size(); }

public slots:
    void propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &) {
        Q_UNUSED(iface)
        if (!changed.contains(QStringLiteral("Strength")))
            return;
        ++m_updates;
        const int strength = unwrap(changed.value(QStringLiteral("Strength"))).toInt();
        std::printf("[%6lld ms] strength update #%d -> %d (%.1f dBm*)\n", (long long)m_clock.elapsed(),
                    m_updates, strength, strength / 2.0 - 100.0);
    }

    void attach(const QString &apPath) {
        QDBusConnection::systemBus().connect(kService, apPath, kPropsIf,
                                            QStringLiteral("PropertiesChanged"), this,
                                            SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
        m_paths.append(apPath);
    }

private:
    QElapsedTimer m_clock;
    int m_updates = 0;
    QStringList m_paths;
};

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int interval = args.size() > 1 ? args.at(1).toInt() : 2;
    const int attempts = args.size() > 2 ? args.at(2).toInt() : 6;
    const int passiveSeconds = args.size() > 3 ? args.at(3).toInt() : 20;

    const QString device = wifiDevicePath();
    if (device.isEmpty()) {
        std::fprintf(stderr, "NetworkManager reports no Wi-Fi device\n");
        return 1;
    }
    QDBusInterface wireless(kService, device, kWifiIf, QDBusConnection::systemBus());
    std::printf("device %s — %d attempts, %d s apart, then %d s passive\n\n",
                qPrintable(device), attempts, interval, passiveSeconds);

    int accepted = 0, refused = 0, moved = 0;
    qlonglong previousLastScan = propertiesOf(device, kWifiIf).value(QStringLiteral("LastScan")).toLongLong();
    std::printf("initial LastScan=%lld  %s\n", previousLastScan, qPrintable(airSummary(device)));

    for (int attempt = 1; attempt <= attempts; ++attempt) {
        QElapsedTimer clock;
        clock.start();
        const QDBusReply<void> reply = wireless.call(QStringLiteral("RequestScan"), QVariantMap());
        const qint64 callMs = clock.elapsed();

        if (reply.isValid()) {
            ++accepted;
            std::printf("attempt %d: accepted in %lld ms\n", attempt, (long long)callMs);
        } else {
            ++refused;
            std::printf("attempt %d: REFUSED in %lld ms — %s: %s\n", attempt, (long long)callMs,
                        qPrintable(reply.error().name()), qPrintable(reply.error().message()));
        }

        QThread::msleep(interval * 1000);
        const qlonglong lastScan = propertiesOf(device, kWifiIf).value(QStringLiteral("LastScan")).toLongLong();
        const bool completed = lastScan != previousLastScan;
        if (completed)
            ++moved;
        std::printf("           LastScan=%lld (%s, +%lld s)  %s\n", lastScan,
                    completed ? "moved" : "UNCHANGED", lastScan - previousLastScan,
                    qPrintable(airSummary(device)));
        previousLastScan = lastScan;
    }

    std::printf("\n%d accepted, %d refused, %d of %d requests actually completed a scan\n",
                accepted, refused, moved, attempts);

    if (passiveSeconds <= 0)
        return 0;

    StrengthCounter counter;
    const QList<QDBusObjectPath> paths = accessPointPaths(device);
    for (const QDBusObjectPath &path : paths)
        counter.attach(path.path());

    std::printf("\n--- %d s passive, no scan requests (%d APs watched) ---\n", passiveSeconds, counter.paths());
    QTimer::singleShot(passiveSeconds * 1000, &app, &QCoreApplication::quit);
    app.exec();
    std::printf("--- %d strength updates in %d s with zero scans requested ---\n",
                counter.updates(), passiveSeconds);
    return 0;
}

#include "main.moc"
