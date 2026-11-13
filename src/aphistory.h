#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

// One measured moment: how many seconds into this session, and how strong.
struct SignalSample {
    qint64 seconds = 0;
    double dbm = -100.0;
};

// Signal over time, per BSSID, as a rolling window. The raw signal swings
// several dB while the machine sits still (spike 003 measured −65.5 → −55.0 dBm
// inside 20 s at a desk), so the raw series is kept and a rolling mean is what
// gets drawn.
class ApHistory : public QObject {
    Q_OBJECT

public:
    explicit ApHistory(QObject *parent = nullptr);

    // Ten minutes is well past any useful "how has it been behaving" question
    // and keeps the whole session's memory tiny.
    static constexpr qint64 kWindowSeconds = 600;

    void record(const QString &bssid, qint64 seconds, double dbm);
    void forget(const QString &bssid);
    void clear();

    QList<SignalSample> samples(const QString &bssid) const;
    QList<SignalSample> window(const QString &bssid, qint64 seconds) const;
    int sampleCount(const QString &bssid) const;
    QStringList bssids() const;

    // Rolling mean over `window` samples, the same length as the input so it
    // lines up with the time axis.
    static QList<double> movingAverage(const QList<double> &values, int window);
    QList<double> smoothedDbm(const QString &bssid, qint64 seconds, int window = 5) const;

    // Summary for the export and the window: empty when nothing was recorded.
    double minimumDbm(const QString &bssid) const;
    double maximumDbm(const QString &bssid) const;

    // Drop series whose last sample is older than the window: an AP that left
    // the air should not keep its history alive forever.
    void prune(qint64 nowSeconds);

signals:
    void changed();

private:
    QHash<QString, QList<SignalSample> > m_series;
};
