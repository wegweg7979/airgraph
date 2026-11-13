#pragma once

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QHash>
#include <QString>
#include <QVariantMap>

#include "wifisource.h"

class NmApWatcher;

// NetworkManager as the Wi-Fi source, over the system bus. Read-only: it asks
// for scans and reads access points, and never connects, disconnects, or
// touches radio or regulatory state. Unprivileged by design — `iw scan` needs
// CAP_NET_ADMIN on this host, NetworkManager does not (spike 001).
class NmWifiSource : public WifiSource {
    Q_OBJECT

public:
    explicit NmWifiSource(QObject *parent = nullptr);
    ~NmWifiSource() override;

    bool available() const override { return m_available; }
    QString problem() const override { return m_problem; }
    QString device() const override { return m_device; }
    QList<AccessPoint> accessPoints() override;
    bool requestScan(QString *error = nullptr) override;
    bool takeScanCompleted() override;

private:
    QVariantMap propertiesOf(const QString &path, const QString &interface) const;
    QList<QDBusObjectPath> accessPointPaths() const;
    void bindToNetworkManager();
    void syncWatchers(const QList<QDBusObjectPath> &paths);

    QDBusConnection m_bus;
    QString m_devicePath;
    QString m_device;
    QString m_problem;
    QString m_connectedBssid;
    QString m_widthBssid;        // whose link width m_linkWidthMhz describes
    int m_linkWidthMhz = 0;
    bool m_available = false;
    bool m_scanCompleted = false;
    qlonglong m_scanStamp = 0;
    QHash<QString, NmApWatcher *> m_watchers;
};
