#pragma once

#include <QList>
#include <QObject>
#include <QString>

// One access point as NetworkManager reports it. NetworkManager has no channel
// width field at all (spike 001 checked every property), so a width here is only
// present when something else measured it: `iw dev <iface> info` reports the
// width of the link the machine is on, and nothing reports the others' without
// CAP_NET_ADMIN.
struct AccessPoint {
    QString bssid;
    QString ssid;
    QString security;
    int frequency = 0;    // MHz
    int signal = 0;       // NetworkManager's 0-100 scale, not dBm
    uint maxBitrate = 0;  // kbit/s
    int widthMhz = 0;     // 0 means unknown, not zero
    bool connected = false;
};

// What the app needs from a Wi-Fi source, kept narrow so the scan backend can
// be tested against a fake and no UI code talks to D-Bus.
class WifiSource : public QObject {
    Q_OBJECT

public:
    explicit WifiSource(QObject *parent = nullptr) : QObject(parent) {}
    ~WifiSource() override = default;

    virtual bool available() const = 0;
    virtual QString problem() const = 0;   // a human-readable blocker; empty when healthy
    virtual QString device() const = 0;    // interface name, e.g. wlp4s0
    virtual QList<AccessPoint> accessPoints() = 0;
    virtual bool requestScan(QString *error = nullptr) = 0;

    // True once since the last call if a scan actually completed. NetworkManager
    // accepts every request and silently coalesces, so this is the only honest
    // way to know the data is fresh (spike 003).
    virtual bool takeScanCompleted() = 0;

signals:
    void changed();   // the set of access points, or one of their properties, moved
};

// Band, channel and a derived dBm, as free functions so the tests need no D-Bus.
int bandOf(int frequency);        // 2, 5 or 6
int channelOf(int frequency);     // channel number, -1 when the frequency is unknown
double dbmOf(int signal);         // signal / 2 - 100 — NetworkManager's own inverse mapping
int assumedWidthOf(int frequency);  // 20 MHz everywhere: NetworkManager does not report width
int widthOf(const AccessPoint &ap);       // the real width when something reported it
bool widthIsAssumed(const AccessPoint &ap);
QString securityOf(uint flags, uint wpaFlags, uint rsnFlags);
