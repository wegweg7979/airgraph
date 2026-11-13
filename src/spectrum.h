#pragma once

#include <QColor>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "wifisource.h"

// The spectrum view's arithmetic, with no painting and no Qt Quick in it, so
// every number the graph draws can be pinned in a test: the frequency axis, the
// dBm axis, the bell drawn for each access point, the per-channel congestion
// score, and the channel the app recommends.
namespace spectrum {

struct Band {
    int gigahertz = 2;
    int minFrequency = 2401;
    int maxFrequency = 2484;
    QList<int> tickFrequencies;   // one per channel this band has

    QString label() const;        // "2.4 GHz", "5 GHz", "6 GHz"
};

Band bandFor(int gigahertz, const QList<int> &channelFrequencies);

// Axis mapping. The frequency axis is linear in MHz, which is what makes 2.4
// GHz channels overlap honestly and 5 GHz gaps visible.
double frequencyToX(int frequency, const Band &band, const QRectF &plot);
double dbmToY(double dbm, const QRectF &plot, double topDbm = -20.0, double bottomDbm = -100.0);

// A bell with a flat top across the assumed channel width and smooth shoulders
// falling to the floor. The width is the *assumed* 20 MHz (NetworkManager does
// not report width), which is why the graph has to say so.
QList<QPointF> bellCurve(int centerFrequency, int widthMhz, double peakDbm, const Band &band,
                         const QRectF &plot, double topDbm = -20.0, double bottomDbm = -100.0);

// Congestion: how much of a channel another AP is sitting on, weighted by how
// strong it is. 0 is empty, 1 is one strong AP dead centre, more means worse.
double channelScore(const QList<AccessPoint> &accessPoints, int channelFrequency);

// The same scale WiFi Analyzer shows: five dots for an empty channel, one for a
// swamped one.
int starsFor(double score);

// The emptiest of the candidates — for 2.4 GHz that is 1/6/11, the only three
// that do not overlap each other.
int bestChannel(const QList<AccessPoint> &accessPoints, const QList<int> &candidates);

// The dBm window an axis should show for a set of values: centred on the data,
// at least `minimumSpan` tall so a steady signal does not look like a mountain
// range, and never outside the usable range. Getting this wrong clips the data
// against an edge and still looks like a plausible graph.
struct DbmRange {
    double top = -30.0;
    double bottom = -100.0;
};

DbmRange rangeFor(const QList<double> &values, double minimumSpan = 20.0,
                  double highest = -25.0, double lowest = -100.0);

// Green / orange / red, the way a signal graph has always read. Vivid enough to
// separate three APs at a glance on a dark plot; the lightness ladder is what
// keeps them apart for a colour-blind reader, since green and orange share a
// hue direction once desaturated.
QColor strengthColor(double dbm);

} // namespace spectrum
