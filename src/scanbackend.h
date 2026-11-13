#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include "apfiltermodel.h"
#include "apmodel.h"

class WifiSource;

// The scan cadence and the freshness clock.
//
// One RequestScan every scanIntervalSeconds, plus a lighter refresh that
// re-reads what NetworkManager already holds, plus a signal subscription so a
// completed scan lands on screen the moment it arrives. The cadence is 10 s
// because NetworkManager accepts faster requests and silently coalesces them:
// in the M0 spike six requests 2 s apart were all accepted and only two
// actually scanned, so a faster poll would be a lie (spikes/003).
class ScanBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString device READ device NOTIFY stateChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY stateChanged)
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged)
    Q_PROPERTY(bool waitingForScan READ waitingForScan NOTIFY waitingForScanChanged)
    Q_PROPERTY(int secondsSinceScan READ secondsSinceScan NOTIFY secondsSinceScanChanged)
    Q_PROPERTY(QString scanError READ scanError NOTIFY scanErrorChanged)
    Q_PROPERTY(int scanIntervalSeconds READ scanIntervalSeconds CONSTANT)
    Q_PROPERTY(int gigahertz READ gigahertz WRITE setGigahertz NOTIFY gigahertzChanged)
    Q_PROPERTY(ApModel *accessPoints READ accessPoints CONSTANT)
    Q_PROPERTY(ApFilterModel *bandAccessPoints READ bandAccessPoints CONSTANT)

public:
    explicit ScanBackend(WifiSource *source, QObject *parent = nullptr);

    QString device() const;
    QString problem() const;
    bool available() const;
    bool live() const { return m_live; }
    void setLive(bool live);
    bool waitingForScan() const { return m_waiting; }
    int secondsSinceScan() const { return m_secondsSinceScan; }
    QString scanError() const { return m_scanError; }
    int scanIntervalSeconds() const { return m_scanIntervalSeconds; }
    int gigahertz() const { return m_bandAccessPoints.gigahertz(); }
    void setGigahertz(int gigahertz);
    ApModel *accessPoints() { return &m_accessPoints; }
    ApFilterModel *bandAccessPoints() { return &m_bandAccessPoints; }

    Q_INVOKABLE void rescan();

    // Re-read what NetworkManager has and fold it into the model. Public so the
    // tests can drive it without timers.
    void refreshNow();

signals:
    void stateChanged();
    void liveChanged();
    void waitingForScanChanged();
    void secondsSinceScanChanged();
    void scanErrorChanged();
    void gigahertzChanged();

private:
    void tick();
    void askForScan();

    static constexpr int kRefreshMs = 3000;
    static constexpr int kClockMs = 1000;

    WifiSource *m_source = nullptr;
    ApModel m_accessPoints;
    ApFilterModel m_bandAccessPoints;
    QTimer m_refresh;
    QTimer m_cadence;
    QTimer m_clock;
    bool m_live = false;
    bool m_waiting = false;
    bool m_bandChosen = false;
    int m_secondsSinceScan = 0;
    int m_scanIntervalSeconds = 10;
    QString m_scanError;
};
