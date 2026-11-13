#include "spectrum.h"

#include <QtMath>

#include <algorithm>

namespace spectrum {

QString Band::label() const {
    switch (gigahertz) {
    case 5:  return QStringLiteral("5 GHz");
    case 6:  return QStringLiteral("6 GHz");
    default: return QStringLiteral("2.4 GHz");
    }
}

Band bandFor(int gigahertz, const QList<int> &channelFrequencies) {
    Band band;
    band.gigahertz = gigahertz;
    band.tickFrequencies = channelFrequencies;
    if (!channelFrequencies.isEmpty()) {
        band.minFrequency = channelFrequencies.first();
        band.maxFrequency = channelFrequencies.last();
    }
    // A little air either side so the end curves are not clipped by the edge.
    const int margin = gigahertz == 2 ? 6 : 24;
    band.minFrequency -= margin;
    band.maxFrequency += margin;
    return band;
}

double frequencyToX(int frequency, const Band &band, const QRectF &plot) {
    const double span = band.maxFrequency - band.minFrequency;
    if (span <= 0)
        return plot.left();
    const double fraction = (frequency - band.minFrequency) / span;
    return plot.left() + fraction * plot.width();
}

double dbmToY(double dbm, const QRectF &plot, double topDbm, double bottomDbm) {
    const double span = topDbm - bottomDbm;   // positive: dBm grows upward
    if (span == 0)
        return plot.bottom();
    const double clamped = qBound(bottomDbm, dbm, topDbm);
    const double fraction = (clamped - bottomDbm) / span;
    return plot.bottom() - fraction * plot.height();
}

QList<QPointF> bellCurve(int centerFrequency, int widthMhz, double peakDbm, const Band &band,
                         const QRectF &plot, double topDbm, double bottomDbm) {
    QList<QPointF> points;
    const double half = widthMhz / 2.0;
    const int steps = 32;

    auto pointAt = [&](double delta) {
        double dbm = peakDbm;
        const double distance = qAbs(delta);
        if (distance > half) {
            // Smoothstep down the shoulder: flat top, gentle skirt, floor at
            // one channel width out.
            const double t = qBound(0.0, (distance - half) / half, 1.0);
            const double drop = 20.0 * (t * t * (3.0 - 2.0 * t));
            dbm = peakDbm - drop;
        }
        return QPointF(frequencyToX(qRound(centerFrequency + delta), band, plot),
                       dbmToY(dbm, plot, topDbm, bottomDbm));
    };

    points.reserve(steps + 1);
    for (int step = 0; step <= steps; ++step) {
        const double delta = -double(widthMhz) + (2.0 * widthMhz) * step / steps;
        points.append(pointAt(delta));
    }
    return points;
}

double channelScore(const QList<AccessPoint> &accessPoints, int channelFrequency) {
    constexpr double kOverlapWindow = 25.0;   // MHz: how far an AP's energy reaches
    constexpr double kNarrowest = -100.0, kStrongest = -30.0;
    double score = 0.0;

    for (const AccessPoint &ap : accessPoints) {
        if (ap.frequency <= 0)
            continue;
        const double overlap = 1.0 - qAbs(double(ap.frequency - channelFrequency)) / kOverlapWindow;
        if (overlap <= 0.0)
            continue;
        // Stronger APs count for more, on a 0-1 scale across the usable range.
        const double weight = qBound(0.0, (dbmOf(ap.signal) - kNarrowest) / (kStrongest - kNarrowest), 1.0);
        score += overlap * weight;
    }
    return score;
}

int starsFor(double score) {
    if (score <= 0.15)
        return 5;
    if (score <= 0.40)
        return 4;
    if (score <= 0.70)
        return 3;
    if (score <= 1.10)
        return 2;
    return 1;
}

int bestChannel(const QList<AccessPoint> &accessPoints, const QList<int> &candidates) {
    int best = -1;
    double bestScore = 0.0;
    for (int frequency : candidates) {
        const double score = channelScore(accessPoints, frequency);
        if (best < 0 || score < bestScore) {
            best = frequency;
            bestScore = score;
        }
    }
    return best;
}

DbmRange rangeFor(const QList<double> &values, double minimumSpan, double highest, double lowest) {
    DbmRange range;
    if (values.isEmpty()) {
        range.top = highest;
        range.bottom = qMax(lowest, highest - minimumSpan);
        return range;
    }

    double minimum = values.first(), maximum = values.first();
    for (double value : values) {
        minimum = qMin(minimum, value);
        maximum = qMax(maximum, value);
    }

    const double span = qMax(minimumSpan, maximum - minimum + 8.0);
    const double middle = (minimum + maximum) / 2.0;
    range.top = middle + span / 2.0;
    range.bottom = range.top - span;

    // Slide the window back inside the usable band rather than clamping its
    // edges, which would flatten the data against the edge.
    if (range.top > highest) {
        range.top = highest;
        range.bottom = range.top - span;
    }
    if (range.bottom < lowest) {
        range.bottom = lowest;
        range.top = range.bottom + span;
    }
    return range;
}

QColor strengthColor(double dbm) {
    if (dbm >= -60.0)
        return QColor(0x7f, 0xd1, 0x8a);   // green: you could use this
    if (dbm >= -75.0)
        return QColor(0xe6, 0xc0, 0x6a);   // amber: usable
    return QColor(0xe0, 0x7a, 0x7a);       // red: barely there
}

} // namespace spectrum
