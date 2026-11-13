#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

// The channels this radio actually has, read from `iw phy` — a query that needs
// no privilege — with a standard channel table as the fallback when `iw` is
// missing or the output changes shape. Capability comes from the phy;
// observations come from NetworkManager; the two are reported separately.
class BandPlan : public QObject {
    Q_OBJECT

public:
    explicit BandPlan(QObject *parent = nullptr);

    // Runs `iw dev <device> info` then `iw phy <phy> info` once. Safe to call
    // with an empty device: the plan stays on the standard table.
    void load(const QString &device);

    QList<int> frequencies(int gigahertz) const;   // MHz, empty when the phy has none
    bool advertised(int gigahertz) const;
    QString source() const { return m_source; }
    int count(int gigahertz) const { return int(frequencies(gigahertz).size()); }

    // `iw phy` text → 2 / 5 / 6 GHz frequencies. Band 1 is 2.4 GHz, Band 2 is
    // 5 GHz, Band 4 is 6 GHz; the frequencies are printed as "5955.0 MHz [1]".
    static QHash<int, QList<int> > parse(const QString &iwPhyInfo);

    static QList<int> standard(int gigahertz);

    // `iw dev <iface> info` text → the current link. Pure, so the tests can pin
    // it against real output.
    static int parseLinkWidth(const QString &iwDevInfo);
    static int parseLinkFrequency(const QString &iwDevInfo);
    static int parseLinkCenterFrequency(const QString &iwDevInfo);
    static QString parseLinkSsid(const QString &iwDevInfo);
    static int currentLinkWidth(const QString &device);

private:
    QString m_source = QStringLiteral("standard channel table");
    QHash<int, QList<int> > m_frequencies;
};
