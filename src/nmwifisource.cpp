// NetworkManager as the Wi-Fi source. The D-Bus shapes here are the ones the
// M0 spike proved on this hardware: values arrive wrapped in a QDBusVariant,
// the access-point list comes from GetAccessPoints (falling back to the
// AccessPoints property, which arrives as a QDBusArgument), and every scan
// result shows up later as signals on the access-point paths.

#include "nmwifisource.h"

#include "bandplan.h"

#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QMetaType>

namespace {

const QString kService  = QStringLiteral("org.freedesktop.NetworkManager");
const QString kRoot     = QStringLiteral("/org/freedesktop/NetworkManager");
const QString kDeviceIf = QStringLiteral("org.freedesktop.NetworkManager.Device");
const QString kWifiIf   = QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless");
const QString kApIf     = QStringLiteral("org.freedesktop.NetworkManager.AccessPoint");
const QString kPropsIf  = QStringLiteral("org.freedesktop.DBus.Properties");

enum DeviceType { WifiDeviceType = 2 };

// GetAll wraps every value in a QDBusVariant, so unwrap before reading it.
QVariant unwrap(QVariant value) {
    if (value.metaType().id() == qMetaTypeId<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    return value;
}

} // namespace

// One listener per access point: NetworkManager pushes Strength and LastSeen
// changes on the AP path, and the window renders on arrival instead of polling
// for them (spike 003).
class NmApWatcher : public QObject {
    Q_OBJECT

public:
    explicit NmApWatcher(NmWifiSource *source, QObject *parent = nullptr)
        : QObject(parent), m_source(source) {}

    void listen(const QString &accessPointPath) {
        QDBusConnection::systemBus().connect(kService, accessPointPath, kPropsIf,
                                             QStringLiteral("PropertiesChanged"), this,
                                             SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    }

public slots:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &) {
        Q_UNUSED(interface)
        if (!changed.contains(QStringLiteral("Strength")) && !changed.contains(QStringLiteral("LastSeen")))
            return;
        emit m_source->changed();
    }

private:
    NmWifiSource *m_source;
};

NmWifiSource::NmWifiSource(QObject *parent)
    : WifiSource(parent), m_bus(QDBusConnection::systemBus()) {
    bindToNetworkManager();
}

NmWifiSource::~NmWifiSource() = default;

void NmWifiSource::bindToNetworkManager() {
    if (!m_bus.isConnected()) {
        m_problem = tr("Cannot reach the system bus, so NetworkManager cannot be asked.");
        return;
    }

    QDBusInterface manager(kService, kRoot, kService, m_bus);
    if (!manager.isValid()) {
        m_problem = tr("NetworkManager is not running, and it is what scans here.");
        return;
    }

    const QDBusReply<QList<QDBusObjectPath> > devices = manager.call(QStringLiteral("GetDevices"));
    if (!devices.isValid()) {
        m_problem = tr("NetworkManager did not list its devices: %1").arg(devices.error().message());
        return;
    }

    const QList<QDBusObjectPath> paths = devices.value();
    for (const QDBusObjectPath &path : paths) {
        const QVariantMap properties = propertiesOf(path.path(), kDeviceIf);
        if (properties.value(QStringLiteral("DeviceType")).toUInt() != WifiDeviceType)
            continue;
        if (m_devicePath.isEmpty()) {
            m_devicePath = path.path();
            m_device = properties.value(QStringLiteral("Interface")).toString();
        }
    }

    if (m_devicePath.isEmpty()) {
        m_problem = tr("This machine has no Wi-Fi device that NetworkManager knows about.");
        return;
    }

    const QVariantMap managerProperties = propertiesOf(kRoot, kService);
    if (!managerProperties.value(QStringLiteral("WirelessEnabled")).toBool()) {
        m_problem = tr("Wi-Fi is switched off. Turn the radio on and scan again.");
        return;
    }

    // The device appearing and disappearing is the cheapest structural signal;
    // scanning a list of paths every few seconds is left to the backend's cadence.
    m_bus.connect(kService, m_devicePath, kWifiIf, QStringLiteral("AccessPointAdded"), this,
                  SIGNAL(changed()));
    m_bus.connect(kService, m_devicePath, kWifiIf, QStringLiteral("AccessPointRemoved"), this,
                  SIGNAL(changed()));

    m_available = true;
}

QVariantMap NmWifiSource::propertiesOf(const QString &path, const QString &interface) const {
    QVariantMap result;
    QDBusInterface properties(kService, path, kPropsIf, m_bus);
    const QDBusReply<QVariantMap> reply = properties.call(QStringLiteral("GetAll"), interface);
    if (!reply.isValid())
        return result;
    const QVariantMap raw = reply.value();
    for (auto it = raw.cbegin(); it != raw.cend(); ++it)
        result.insert(it.key(), unwrap(it.value()));
    return result;
}

QList<QDBusObjectPath> NmWifiSource::accessPointPaths() const {
    if (m_devicePath.isEmpty())
        return {};

    QDBusInterface wireless(kService, m_devicePath, kWifiIf, m_bus);
    const QDBusReply<QList<QDBusObjectPath> > byMethod =
        wireless.call(QStringLiteral("GetAccessPoints"));
    if (byMethod.isValid())
        return byMethod.value();

    const QVariant raw = propertiesOf(m_devicePath, kWifiIf).value(QStringLiteral("AccessPoints"));
    if (raw.canConvert<QList<QDBusObjectPath> >())
        return raw.value<QList<QDBusObjectPath> >();

    QList<QDBusObjectPath> paths;
    if (raw.metaType().id() == qMetaTypeId<QDBusArgument>())
        raw.value<QDBusArgument>() >> paths;
    return paths;
}

void NmWifiSource::syncWatchers(const QList<QDBusObjectPath> &paths) {
    QHash<QString, NmApWatcher *> keep;
    for (const QDBusObjectPath &path : paths) {
        NmApWatcher *watcher = m_watchers.take(path.path());
        if (!watcher) {
            watcher = new NmApWatcher(this, this);
            watcher->listen(path.path());
        }
        keep.insert(path.path(), watcher);
    }
    for (NmApWatcher *stale : std::as_const(m_watchers))
        stale->deleteLater();
    m_watchers = keep;
}

QList<AccessPoint> NmWifiSource::accessPoints() {
    QList<AccessPoint> result;
    if (!m_available)
        return result;

    const QList<QDBusObjectPath> paths = accessPointPaths();
    syncWatchers(paths);

    // The associated AP is marked so the table can single it out.
    m_connectedBssid.clear();
    const QVariant active = propertiesOf(m_devicePath, kWifiIf).value(QStringLiteral("ActiveAccessPoint"));
    const QString activePath = active.value<QDBusObjectPath>().path();
    if (!activePath.isEmpty())
        m_connectedBssid = propertiesOf(activePath, kApIf).value(QStringLiteral("HwAddress")).toString();

    // The one real channel width available without privileges: what `iw dev`
    // reports for the link the machine is on. Re-read when the association
    // changes, not on every refresh — it cannot change otherwise.
    if (m_connectedBssid != m_widthBssid || m_linkWidthMhz <= 0) {
        m_linkWidthMhz = BandPlan::currentLinkWidth(m_device);
        m_widthBssid = m_connectedBssid;
    }

    for (const QDBusObjectPath &path : paths) {
        const QVariantMap properties = propertiesOf(path.path(), kApIf);
        if (properties.isEmpty())
            continue;

        AccessPoint ap;
        ap.bssid = properties.value(QStringLiteral("HwAddress")).toString();
        ap.ssid = QString::fromUtf8(properties.value(QStringLiteral("Ssid")).toByteArray());
        ap.frequency = properties.value(QStringLiteral("Frequency")).toInt();
        ap.signal = properties.value(QStringLiteral("Strength")).toInt();
        ap.maxBitrate = properties.value(QStringLiteral("MaxBitrate")).toUInt();
        ap.security = securityOf(properties.value(QStringLiteral("Flags")).toUInt(),
                                 properties.value(QStringLiteral("WpaFlags")).toUInt(),
                                 properties.value(QStringLiteral("RsnFlags")).toUInt());
        ap.connected = !m_connectedBssid.isEmpty()
                       && ap.bssid.compare(m_connectedBssid, Qt::CaseInsensitive) == 0;
        // Only the link we are on has a measured width; every other AP's stays
        // unknown and is drawn as the stated assumption.
        if (ap.connected)
            ap.widthMhz = m_linkWidthMhz;
        result.append(ap);
    }

    // LastScan only moves when a scan really ran: NetworkManager accepts extra
    // requests and coalesces them, so this is the freshness signal (spike 003).
    const qlonglong stamp = propertiesOf(m_devicePath, kWifiIf).value(QStringLiteral("LastScan")).toLongLong();
    if (m_scanStamp != 0 && stamp != m_scanStamp)
        m_scanCompleted = true;
    m_scanStamp = stamp;

    return result;
}

bool NmWifiSource::requestScan(QString *error) {
    if (!m_available) {
        if (error)
            *error = m_problem;
        return false;
    }

    QDBusInterface wireless(kService, m_devicePath, kWifiIf, m_bus);
    const QDBusReply<void> reply = wireless.call(QStringLiteral("RequestScan"), QVariantMap());
    if (!reply.isValid()) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(reply.error().name(), reply.error().message());
        return false;
    }
    return true;
}

bool NmWifiSource::takeScanCompleted() {
    const bool completed = m_scanCompleted;
    m_scanCompleted = false;
    return completed;
}

#include "nmwifisource.moc"
