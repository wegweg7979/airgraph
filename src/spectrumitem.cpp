#include "spectrumitem.h"

#include "apmodel.h"
#include "bandplan.h"
#include "spectrum.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

namespace {

const QColor kText(0xd8, 0xd8, 0xd8);
const QColor kDim(0x8a, 0x8a, 0x8a);
const QColor kGrid(0x2b, 0x2b, 0x30);
const QColor kAxis(0x3d, 0x3d, 0x44);

struct PlottedAp {
    QString ssid;
    int frequency = 0;
    int width = 20;
    bool widthAssumed = true;
    double dbm = -100.0;
    bool connected = false;
};

} // namespace

SpectrumItem::SpectrumItem(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
}

void SpectrumItem::setModel(ApModel *model) {
    if (m_model == model)
        return;
    if (m_model)
        m_model->disconnect(this);
    m_model = model;
    if (m_model) {
        // Any change the model makes is worth a repaint: rows coming and going,
        // values moving, and the reordering that keeps the strongest on top.
        connect(m_model, &QAbstractItemModel::rowsInserted, this, [this] { update(); });
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, [this] { update(); });
        connect(m_model, &QAbstractItemModel::dataChanged, this, [this] { update(); });
        connect(m_model, &QAbstractItemModel::layoutChanged, this, [this] { update(); });
        connect(m_model, &QAbstractItemModel::modelReset, this, [this] { update(); });
    }
    emit modelChanged();
    update();
}

void SpectrumItem::setBandPlan(BandPlan *bandPlan) {
    if (m_bandPlan == bandPlan)
        return;
    m_bandPlan = bandPlan;
    emit bandPlanChanged();
    update();
}

void SpectrumItem::setGigahertz(int gigahertz) {
    if (m_gigahertz == gigahertz)
        return;
    m_gigahertz = gigahertz;
    emit gigahertzChanged();
    update();
}

void SpectrumItem::setAccent(const QString &accent) {
    if (m_accent == accent)
        return;
    m_accent = accent;
    emit accentChanged();
    update();
}

void SpectrumItem::setRadius(int radius) {
    if (m_radius == radius)
        return;
    m_radius = radius;
    emit radiusChanged();
    update();
}

int SpectrumItem::heightHint() const {
    return 300;
}

void SpectrumItem::paint(QPainter *painter) {
    const QRectF bounds = contentsBoundingRect();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QList<int> ticks = m_bandPlan ? m_bandPlan->frequencies(m_gigahertz)
                                        : BandPlan::standard(m_gigahertz);
    const spectrum::Band band = spectrum::bandFor(m_gigahertz, ticks);

    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0x12, 0x12, 0x16));
    const qreal corner = qMin(8, m_radius);
    painter->drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), corner, corner);

    // Geometry: room on the left for dBm, at the bottom for channel numbers and
    // the congestion strip.
    constexpr qreal leftMargin = 48, rightMargin = 12, topMargin = 28, stripGap = 22;
    constexpr qreal stripHeight = 26, channelLabelHeight = 16;
    const QRectF plot(leftMargin, topMargin,
                      qMax(10.0, bounds.width() - leftMargin - rightMargin),
                      qMax(10.0, bounds.height() - topMargin - stripGap - stripHeight - channelLabelHeight - 8));
    const QRectF strip(plot.left(), plot.bottom() + stripGap, plot.width(), stripHeight);

    // --- the dBm axis ------------------------------------------------------
    QFont axisFont = painter->font();
    axisFont.setPixelSize(11);
    painter->setFont(axisFont);
    for (int dbm = -30; dbm >= -100; dbm -= 10) {
        const qreal y = spectrum::dbmToY(dbm, plot);
        painter->setPen(QPen(kGrid, 1));
        painter->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter->setPen(kDim);
        painter->drawText(QRectF(0, y - 8, leftMargin - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                          QStringLiteral("%1").arg(dbm));
    }
    painter->setPen(QPen(kAxis, 1));
    painter->drawLine(plot.topLeft(), plot.bottomLeft());

    // --- the channel axis --------------------------------------------------
    for (int tick : ticks) {
        const qreal x = spectrum::frequencyToX(tick, band, plot);
        painter->setPen(QPen(kGrid, 1));
        painter->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter->setPen(kDim);
        painter->drawText(QRectF(x - 16, plot.bottom() + 2, 32, channelLabelHeight),
                          Qt::AlignHCenter | Qt::AlignTop, QString::number(channelOf(tick)));
    }

    // --- the access points -------------------------------------------------
    QList<PlottedAp> plotted;
    QList<AccessPoint> accessPoints;
    if (m_model) {
        for (int row = 0; row < m_model->rowCount(); ++row) {
            const QModelIndex index = m_model->index(row, 0);
            if (index.data(ApModel::BandRole).toInt() != m_gigahertz)
                continue;

            PlottedAp ap;
            ap.ssid = index.data(ApModel::SsidRole).toString();
            ap.frequency = index.data(ApModel::FrequencyRole).toInt();
            ap.width = index.data(ApModel::WidthRole).toInt();
            ap.widthAssumed = index.data(ApModel::WidthAssumedRole).toBool();
            ap.dbm = index.data(ApModel::DbmRole).toDouble();
            ap.connected = index.data(ApModel::ConnectedRole).toBool();
            plotted.append(ap);

            AccessPoint raw;
            raw.bssid = index.data(ApModel::BssidRole).toString();
            raw.frequency = ap.frequency;
            raw.signal = index.data(ApModel::SignalRole).toInt();
            accessPoints.append(raw);
        }
    }

    const QColor accent(m_accent);
    // Tallest bell first: the fills are near-opaque, and APs on the same channel
    // share a footprint, so the one painted last is the one you see. Painting
    // the tallest first keeps every shorter neighbour visible inside it.
    std::sort(plotted.begin(), plotted.end(),
              [](const PlottedAp &a, const PlottedAp &b) { return spectrum::tallerFirst(a.dbm, b.dbm); });

    painter->save();
    painter->setClipRect(plot);

    // Two passes: every fill first, then every outline. Two access points on the
    // same channel are genuinely in the same place, and without this the taller
    // bell would simply hide the shorter one instead of nesting inside it.
    for (const PlottedAp &ap : plotted) {
        const QList<QPointF> curve = spectrum::bellCurve(ap.frequency, ap.width, ap.dbm, band, plot);
        QPainterPath path(curve.first());
        for (const QPointF &point : curve)
            path.lineTo(point);
        path.lineTo(curve.last().x(), spectrum::dbmToY(-100.0, plot));
        path.lineTo(curve.first().x(), spectrum::dbmToY(-100.0, plot));
        path.closeSubpath();

        QColor fill = spectrum::strengthColor(ap.dbm);
        // Nearly opaque on purpose: these colours are vivid, but a vivid
        // orange at 30% over a near-black plot composites to brown (#7d5120,
        // measured), and stacking four translucent bells — normal at 2.4 GHz —
        // muddies them further toward grey. At this alpha what is on the
        // screen is the colour, and stacked bells converge on it instead of
        // averaging away from it. Nesting still reads, because every outline
        // is drawn after every fill, in the second pass below.
        fill.setAlpha(ap.connected ? 185 : 215);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->drawPath(path);
    }

    for (const PlottedAp &ap : plotted) {
        const QList<QPointF> curve = spectrum::bellCurve(ap.frequency, ap.width, ap.dbm, band, plot);
        QPainterPath path(curve.first());
        for (const QPointF &point : curve)
            path.lineTo(point);

        QColor stroke = ap.connected ? accent : spectrum::strengthColor(ap.dbm);
        stroke.setAlpha(ap.connected ? 245 : 225);
        painter->setPen(QPen(stroke, ap.connected ? 2.2 : 1.4));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    }
    painter->restore();

    // --- names, strongest first, skipping what would collide ---------------
    std::sort(plotted.begin(), plotted.end(),
              [](const PlottedAp &a, const PlottedAp &b) { return a.dbm > b.dbm; });
    QFont labelFont = painter->font();
    labelFont.setPixelSize(11);
    painter->setFont(labelFont);
    const QFontMetricsF metrics(labelFont);
    qreal lastRight = -1e9;
    for (const PlottedAp &ap : plotted) {
        const QString name = ap.ssid.isEmpty() ? tr("(hidden)") : ap.ssid;
        const qreal width = metrics.horizontalAdvance(name) + 6;
        const qreal x = spectrum::frequencyToX(ap.frequency, band, plot);
        const qreal left = qBound(plot.left(), x - width / 2, plot.right() - width);
        if (left < lastRight + 6)
            continue;
        lastRight = left + width;
        painter->setPen(ap.connected ? accent : kText);
        painter->drawText(QRectF(left, plot.top() + 2, width, 14), Qt::AlignLeft | Qt::AlignVCenter, name);
    }

    // --- the congestion strip ---------------------------------------------
    painter->setFont(axisFont);
    painter->setPen(kDim);
    painter->drawText(QRectF(0, strip.top(), leftMargin - 6, stripHeight),
                      Qt::AlignRight | Qt::AlignVCenter, tr("rating"));
    painter->setPen(QPen(kGrid, 1));
    painter->drawLine(QPointF(strip.left(), strip.bottom() + 0.5), QPointF(strip.right(), strip.bottom() + 0.5));
    for (int tick : ticks) {
        const double score = spectrum::channelScore(accessPoints, tick);
        const int stars = spectrum::starsFor(score);
        const qreal x = spectrum::frequencyToX(tick, band, plot);
        const qreal cellWidth = qMax(14.0, plot.width() / qMax(1, ticks.size()) - 4);

        const double height = qBound(0.0, score / 1.4, 1.0) * (stripHeight - 2);
        const QRectF bar(x - cellWidth / 2, strip.bottom() - height, cellWidth, qMax(2.0, height));
        QColor barColour = accent;
        barColour.setAlpha(stars >= 4 ? 210 : stars == 3 ? 150 : 90);
        painter->setPen(Qt::NoPen);
        painter->setBrush(barColour);
        painter->drawRect(bar);

        QString dots;
        for (int star = 0; star < 5; ++star)
            dots.append(star < stars ? QStringLiteral("●") : QStringLiteral("○"));
        painter->setPen(stars >= 4 ? accent : kDim);
        painter->drawText(QRectF(x - 26, strip.bottom() + 2, 52, channelLabelHeight),
                          Qt::AlignHCenter | Qt::AlignTop, dots);
    }

    // --- the recommendation, and the honest empty states ------------------
    QFont summaryFont = painter->font();
    summaryFont.setPixelSize(12);
    painter->setFont(summaryFont);

    QString summary;
    if (plotted.isEmpty()) {
        if (!m_bandPlan || m_bandPlan->frequencies(m_gigahertz).isEmpty())
            summary = tr("%1: no access points heard, and this radio has no such band").arg(band.label());
        else
            summary = tr("%1: no access points heard (%2 channels this radio can hear)")
                          .arg(band.label()).arg(ticks.size());
    } else {
        QList<int> candidates;
        if (m_gigahertz == 2) {
            for (int tick : ticks) {
                const int channel = channelOf(tick);
                if (channel == 1 || channel == 6 || channel == 11)
                    candidates.append(tick);
            }
        }
        if (candidates.isEmpty())
            candidates = ticks;
        const int best = spectrum::bestChannel(accessPoints, candidates);
        summary = m_gigahertz == 2
                      ? tr("%1 · %n access point(s) · cleanest of 1/6/11: channel %2", "", accessPoints.size())
                            .arg(band.label()).arg(channelOf(best))
                      : tr("%1 · %n access point(s) · cleanest: channel %2", "", accessPoints.size())
                            .arg(band.label()).arg(channelOf(best));

        // Width is the one number that is measured for this machine's own link
        // and assumed for everyone else's, so the graph says which is which.
        int measured = 0;
        for (const PlottedAp &ap : plotted)
            if (!ap.widthAssumed)
                measured = ap.width;
        summary += measured > 0
                       ? tr(" · your link is %1 MHz wide, the rest assumed 20 MHz").arg(measured)
                       : tr(" · widths assumed 20 MHz (only your own link's is reported)");
    }
    painter->setPen(plotted.isEmpty() ? kDim : kText);
    painter->drawText(QRectF(leftMargin, 4, bounds.width() - leftMargin - rightMargin, 18),
                      Qt::AlignLeft | Qt::AlignVCenter, summary);
}
