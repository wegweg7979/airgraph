#include "aphistory.h"

#include <algorithm>

ApHistory::ApHistory(QObject *parent) : QObject(parent) {}

void ApHistory::record(const QString &bssid, qint64 seconds, double dbm) {
    if (bssid.isEmpty())
        return;

    QList<SignalSample> &series = m_series[bssid.toUpper()];
    series.append({ seconds, dbm });

    // Drop what has fallen out of the window, so a long session does not grow
    // without bound.
    while (series.size() > 1 && seconds - series.first().seconds > kWindowSeconds)
        series.removeFirst();

    emit changed();
}

void ApHistory::forget(const QString &bssid) {
    if (m_series.remove(bssid.toUpper()) > 0)
        emit changed();
}

void ApHistory::clear() {
    if (m_series.isEmpty())
        return;
    m_series.clear();
    emit changed();
}

QList<SignalSample> ApHistory::samples(const QString &bssid) const {
    return m_series.value(bssid.toUpper());
}

QList<SignalSample> ApHistory::window(const QString &bssid, qint64 seconds) const {
    const QList<SignalSample> all = samples(bssid);
    if (all.isEmpty())
        return all;

    const qint64 cutoff = all.last().seconds - seconds;
    QList<SignalSample> kept;
    for (const SignalSample &sample : all)
        if (sample.seconds >= cutoff)
            kept.append(sample);
    return kept;
}

int ApHistory::sampleCount(const QString &bssid) const {
    return int(m_series.value(bssid.toUpper()).size());
}

QStringList ApHistory::bssids() const {
    return m_series.keys();
}

QList<double> ApHistory::movingAverage(const QList<double> &values, int window) {
    QList<double> averaged;
    averaged.reserve(values.size());
    if (values.isEmpty())
        return averaged;

    const int span = qMax(1, window);
    for (int i = 0; i < values.size(); ++i) {
        const int from = qMax(0, i - span + 1);
        double sum = 0.0;
        for (int j = from; j <= i; ++j)
            sum += values.at(j);
        averaged.append(sum / (i - from + 1));
    }
    return averaged;
}

QList<double> ApHistory::smoothedDbm(const QString &bssid, qint64 seconds, int window) const {
    QList<double> values;
    const QList<SignalSample> kept = this->window(bssid, seconds);
    values.reserve(kept.size());
    for (const SignalSample &sample : kept)
        values.append(sample.dbm);
    return movingAverage(values, window);
}

double ApHistory::minimumDbm(const QString &bssid) const {
    const QList<SignalSample> all = samples(bssid);
    if (all.isEmpty())
        return 0.0;
    double lowest = all.first().dbm;
    for (const SignalSample &sample : all)
        lowest = qMin(lowest, sample.dbm);
    return lowest;
}

double ApHistory::maximumDbm(const QString &bssid) const {
    const QList<SignalSample> all = samples(bssid);
    if (all.isEmpty())
        return 0.0;
    double highest = all.first().dbm;
    for (const SignalSample &sample : all)
        highest = qMax(highest, sample.dbm);
    return highest;
}

void ApHistory::prune(qint64 nowSeconds) {
    bool dropped = false;
    for (auto it = m_series.begin(); it != m_series.end();) {
        if (it.value().isEmpty() || nowSeconds - it.value().last().seconds > kWindowSeconds) {
            it = m_series.erase(it);
            dropped = true;
        } else {
            ++it;
        }
    }
    if (dropped)
        emit changed();
}
