#include "historyitem.h"

#include "spectrum.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace {

const QColor kText(0xd8, 0xd8, 0xd8);
const QColor kDim(0x8a, 0x8a, 0x8a);
const QColor kGrid(0x2b, 0x2b, 0x30);
const QColor kRaw(0x6a, 0x6a, 0x74);

} // namespace

HistoryItem::HistoryItem(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
}

void HistoryItem::setHistory(ApHistory *history) {
    if (m_history == history)
        return;
    if (m_history)
        m_history->disconnect(this);
    m_history = history;
    if (m_history)
        connect(m_history, &ApHistory::changed, this, [this] { update(); });
    emit historyChanged();
    update();
}

void HistoryItem::setModel(ApModel *model) {
    if (m_model == model)
        return;
    if (m_model)
        m_model->disconnect(this);
    m_model = model;
    if (m_model) {
        // The name and the "connected" mark come from the model, so repaint
        // when it moves too.
        connect(m_model, &QAbstractItemModel::dataChanged, this, [this] { update(); });
        connect(m_model, &QAbstractItemModel::rowsInserted, this, [this] { update(); });
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, [this] { update(); });
        connect(m_model, &QAbstractItemModel::modelReset, this, [this] { update(); });
    }
    emit modelChanged();
    update();
}

void HistoryItem::setBssid(const QString &bssid) {
    if (m_bssid == bssid)
        return;
    m_bssid = bssid;
    emit bssidChanged();
    update();
}

void HistoryItem::setAccent(const QString &accent) {
    if (m_accent == accent)
        return;
    m_accent = accent;
    emit accentChanged();
    update();
}

void HistoryItem::setRadius(int radius) {
    if (m_radius == radius)
        return;
    m_radius = radius;
    emit radiusChanged();
    update();
}

void HistoryItem::setWindowSeconds(int seconds) {
    const int clamped = qBound(60, seconds, int(ApHistory::kWindowSeconds));
    if (m_windowSeconds == clamped)
        return;
    m_windowSeconds = clamped;
    emit windowSecondsChanged();
    update();
}

int HistoryItem::heightHint() const {
    return 120;
}

void HistoryItem::paint(QPainter *painter) {
    const QRectF bounds = contentsBoundingRect();
    painter->setRenderHint(QPainter::Antialiasing, true);

    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0x12, 0x12, 0x16));
    const qreal corner = qMin(8, m_radius);
    painter->drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), corner, corner);

    constexpr qreal leftMargin = 48, rightMargin = 12, topMargin = 24, bottomMargin = 18;
    const QRectF plot(leftMargin, topMargin,
                      qMax(10.0, bounds.width() - leftMargin - rightMargin),
                      qMax(10.0, bounds.height() - topMargin - bottomMargin));

    QFont labelFont = painter->font();
    labelFont.setPixelSize(11);
    QFont titleFont = painter->font();
    titleFont.setPixelSize(12);

    // Who this is: the model has the name, the history has the numbers.
    QString name = tr("no access point selected");
    bool connected = false;
    QString band;
    if (m_model && !m_bssid.isEmpty()) {
        for (int row = 0; row < m_model->rowCount(); ++row) {
            const QModelIndex index = m_model->index(row, 0);
            if (index.data(ApModel::BssidRole).toString().compare(m_bssid, Qt::CaseInsensitive) != 0)
                continue;
            const QString ssid = index.data(ApModel::SsidRole).toString();
            name = ssid.isEmpty() ? tr("(hidden)") : ssid;
            connected = index.data(ApModel::ConnectedRole).toBool();
            // The band belongs in the title: this panel follows the selected AP,
            // which is not always on the band the graphs above are showing.
            band = spectrum::bandFor(index.data(ApModel::BandRole).toInt(), {}).label();
            break;
        }
    }

    const QList<SignalSample> raw = m_history ? m_history->window(m_bssid, m_windowSeconds)
                                              : QList<SignalSample>();
    const QList<double> smoothed = m_history ? m_history->smoothedDbm(m_bssid, m_windowSeconds)
                                             : QList<double>();

    painter->setFont(titleFont);
    painter->setPen(connected ? QColor(m_accent) : kText);
    const QString minutes = windowSeconds() % 60 == 0 ? tr("last %1 min").arg(windowSeconds() / 60)
                                                      : tr("last %1 s").arg(windowSeconds());
    QString title = m_bssid.isEmpty() ? name
                                      : QStringLiteral("%1 · %2 · %3").arg(name, band, minutes);
    if (!raw.isEmpty())
        title += tr(" · now %1 dBm").arg(raw.last().dbm, 0, 'f', 1);
    painter->drawText(QRectF(leftMargin, 4, bounds.width() - leftMargin - rightMargin, 18),
                      Qt::AlignLeft | Qt::AlignVCenter, title);

    if (raw.size() < 2) {
        painter->setFont(labelFont);
        painter->setPen(kDim);
        painter->drawText(plot, Qt::AlignCenter,
                          tr("collecting… a sample every few seconds, and the line needs two"));
        return;
    }

    // The dBm range follows the data, with a floor on the span so a steady
    // signal does not look like a mountain range. Stated on the axis, since it
    // moves.
    QList<double> values;
    values.reserve(raw.size());
    for (const SignalSample &sample : raw)
        values.append(sample.dbm);
    const spectrum::DbmRange range = spectrum::rangeFor(values);
    const double top = range.top;
    const double bottom = range.bottom;

    double lowest = raw.first().dbm, highest = raw.first().dbm;
    for (const SignalSample &sample : raw) {
        lowest = qMin(lowest, sample.dbm);
        highest = qMax(highest, sample.dbm);
    }

    const qint64 newest = raw.last().seconds;
    const qint64 oldest = qMax(raw.first().seconds, newest - m_windowSeconds);
    const qint64 width = qMax<qint64>(1, newest - oldest);

    auto xFor = [&](qint64 seconds) {
        return plot.left() + double(seconds - oldest) / double(width) * plot.width();
    };
    auto yFor = [&](double dbm) {
        const double fraction = (qBound(bottom, dbm, top) - bottom) / (top - bottom);
        return plot.bottom() - fraction * plot.height();
    };

    // Three grid lines with their real values: the range moves, so the labels
    // are the only honest frame of reference.
    painter->setFont(labelFont);
    for (int step = 0; step <= 2; ++step) {
        const double dbm = top - (top - bottom) * step / 2.0;
        const qreal y = yFor(dbm);
        painter->setPen(QPen(kGrid, 1));
        painter->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter->setPen(kDim);
        painter->drawText(QRectF(0, y - 8, leftMargin - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                          QString::number(dbm, 'f', 0));
    }
    painter->setPen(QPen(kGrid, 1));
    painter->drawLine(QPointF(xFor(oldest), plot.bottom() + 0.5), QPointF(xFor(newest), plot.bottom() + 0.5));
    painter->setPen(kDim);
    painter->drawText(QRectF(plot.left(), plot.bottom() + 2, 120, 16), Qt::AlignLeft | Qt::AlignTop,
                      tr("%1 min ago").arg(double(width) / 60.0, 0, 'f', 1));
    painter->drawText(QRectF(plot.right() - 60, plot.bottom() + 2, 60, 16), Qt::AlignRight | Qt::AlignTop,
                      tr("now"));

    // Raw samples as dots: the noise the line is drawn through.
    painter->setPen(Qt::NoPen);
    painter->setBrush(kRaw);
    for (const SignalSample &sample : raw) {
        const QPointF point(xFor(sample.seconds), yFor(sample.dbm));
        painter->drawEllipse(point, 1.6, 1.6);
    }

    // The rolling mean.
    QPainterPath path;
    for (int i = 0; i < smoothed.size() && i < raw.size(); ++i) {
        const QPointF point(xFor(raw.at(i).seconds), yFor(smoothed.at(i)));
        if (i == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    QColor line(m_accent);
    line.setAlpha(230);
    painter->setPen(QPen(line, 1.8));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);

    // The spread over the window, stated rather than implied.
    painter->setFont(labelFont);
    painter->setPen(kDim);
    painter->drawText(QRectF(plot.right() - 220, 4, 220, 18), Qt::AlignRight | Qt::AlignVCenter,
                      tr("%n sample(s) · spread %1 dB", "", raw.size())
                          .arg(highest - lowest, 0, 'f', 1));
}
