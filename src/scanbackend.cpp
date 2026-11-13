#include "scanbackend.h"

#include "wifisource.h"

ScanBackend::ScanBackend(WifiSource *source, QObject *parent)
    : QObject(parent), m_source(source) {
    m_bandAccessPoints.setSourceModel(&m_accessPoints);
    connect(m_source, &WifiSource::changed, this, &ScanBackend::refreshNow);

    // Signals say "something moved"; this timer says "and also look every few
    // seconds", because NetworkManager only pushes what changed.
    m_refresh.setInterval(kRefreshMs);
    connect(&m_refresh, &QTimer::timeout, this, &ScanBackend::refreshNow);

    m_cadence.setInterval(m_scanIntervalSeconds * 1000);
    connect(&m_cadence, &QTimer::timeout, this, &ScanBackend::askForScan);

    m_clock.setInterval(kClockMs);
    connect(&m_clock, &QTimer::timeout, this, &ScanBackend::tick);

    if (m_source->available()) {
        refreshNow();
        // The first paint should be the air, not whatever NetworkManager had
        // left over from its own scans. Signals bring the results in.
        askForScan();
        m_refresh.start();
        m_clock.start();
    }
}

QString ScanBackend::device() const { return m_source->device(); }
QString ScanBackend::problem() const { return m_source->problem(); }
bool ScanBackend::available() const { return m_source->available(); }

// The band the graph and the table are showing. It opens on the band the
// machine is actually associated on — the one the user cares about first —
// and then stays wherever the user puts it.
void ScanBackend::setGigahertz(int gigahertz) {
    if (m_bandAccessPoints.gigahertz() == gigahertz)
        return;
    m_bandAccessPoints.setGigahertz(gigahertz);
    m_bandChosen = true;
    emit gigahertzChanged();
}

void ScanBackend::setLive(bool live) {
    if (m_live == live)
        return;
    m_live = live;
    if (m_live) {
        askForScan();
        m_cadence.start();
    } else {
        m_cadence.stop();
        m_waiting = false;
        emit waitingForScanChanged();
    }
    emit liveChanged();
}

void ScanBackend::rescan() {
    askForScan();
}

void ScanBackend::askForScan() {
    QString error;
    if (!m_source->requestScan(&error)) {
        if (error != m_scanError) {
            m_scanError = error;
            emit scanErrorChanged();
        }
        return;
    }
    if (!m_scanError.isEmpty()) {
        m_scanError.clear();
        emit scanErrorChanged();
    }
    if (!m_waiting) {
        m_waiting = true;
        emit waitingForScanChanged();
    }
}

void ScanBackend::refreshNow() {
    if (!m_source->available())
        return;

    m_accessPoints.apply(m_source->accessPoints());

    // First read with an association: show that band. After that the user's
    // choice stands.
    if (!m_bandChosen) {
        for (int row = 0; row < m_accessPoints.rowCount(); ++row) {
            const QModelIndex index = m_accessPoints.index(row, 0);
            if (!index.data(ApModel::ConnectedRole).toBool())
                continue;
            setGigahertz(index.data(ApModel::BandRole).toInt());
            break;
        }
        m_bandChosen = true;
    }

    // A scan that really ran resets the age; one that was coalesced away never
    // reaches this line, which is the whole point of asking LastScan.
    if (m_source->takeScanCompleted()) {
        if (m_secondsSinceScan != 0) {
            m_secondsSinceScan = 0;
            emit secondsSinceScanChanged();
        }
        if (m_waiting) {
            m_waiting = false;
            emit waitingForScanChanged();
        }
    }
}

void ScanBackend::tick() {
    ++m_secondsSinceScan;
    emit secondsSinceScanChanged();
}
