#include "apmodel.h"

#include <QDateTime>
#include <QModelIndexList>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>

// Band, channel and dBm as free functions: the tests pin them without D-Bus,
// and the model, the graph and the exporter all have to agree on them.
int bandOf(int frequency) {
    if (frequency >= 5925)
        return 6;
    if (frequency >= 5150)
        return 5;
    return 2;
}

int channelOf(int frequency) {
    if (frequency >= 5950)
        return (frequency - 5950) / 5;   // 6 GHz: channel 1 is 5955 MHz
    if (frequency >= 5000)
        return (frequency - 5000) / 5;   // 5 GHz: channel 36 is 5180 MHz
    if (frequency >= 2400) {
        if (frequency == 2484)
            return 14;                   // channel 14 sits on its own frequency
        return (frequency - 2407) / 5;   // 2.4 GHz: channel 1 is 2412 MHz
    }
    return -1;
}

// NetworkManager reports strength as 0-100, derived from dBm as
// 2 * (dBm + 100). This inverts its own mapping, quantised to 0.5 dB, which is
// fine for an axis and dishonest as a measurement — the window labels it as
// derived.
double dbmOf(int signal) {
    return signal / 2.0 - 100.0;
}

// NetworkManager does not report channel width anywhere, so the app assumes
// 20 MHz and says so rather than implying it measured anything (spike 001).
int assumedWidthOf(int frequency) {
    Q_UNUSED(frequency)
    return 20;
}

// The width to draw and to show: whatever was measured, or the assumption.
int widthOf(const AccessPoint &ap) {
    return ap.widthMhz > 0 ? ap.widthMhz : assumedWidthOf(ap.frequency);
}

bool widthIsAssumed(const AccessPoint &ap) {
    return ap.widthMhz <= 0;
}

QString securityOf(uint flags, uint wpaFlags, uint rsnFlags) {
    constexpr uint kPrivacy = 0x1;
    if (!(flags & kPrivacy))
        return QStringLiteral("open");

    QStringList parts;
    if (wpaFlags)
        parts << QStringLiteral("wpa");
    if (rsnFlags)
        parts << QStringLiteral("rsn");
    return parts.isEmpty() ? QStringLiteral("privacy") : parts.join(QLatin1Char('+'));
}

ApModel::ApModel(QObject *parent) : QAbstractListModel(parent) {}

QVariantMap ApModel::countsByBand() const {
    QVariantMap counts;
    for (int gigahertz : { 2, 5, 6 })
        counts.insert(QString::number(gigahertz), 0);
    for (const Row &row : m_rows) {
        const QString key = QString::number(bandOf(row.ap.frequency));
        counts.insert(key, counts.value(key).toInt() + 1);
    }
    return counts;
}

int ApModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_rows.size());
}

QList<AccessPoint> ApModel::accessPoints() const {
    QList<AccessPoint> result;
    result.reserve(m_rows.size());
    for (const Row &row : m_rows)
        result.append(row.ap);
    return result;
}

int ApModel::rowOfBssid(const QString &bssid) const {
    for (int row = 0; row < m_rows.size(); ++row)
        if (m_rows.at(row).ap.bssid.compare(bssid, Qt::CaseInsensitive) == 0)
            return row;
    return -1;
}

QVariant ApModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();

    const Row &row = m_rows.at(index.row());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    switch (role) {
    case SsidRole:      return row.ap.ssid;
    case BssidRole:     return row.ap.bssid;
    case BandRole:      return bandOf(row.ap.frequency);
    case ChannelRole:   return channelOf(row.ap.frequency);
    case FrequencyRole: return row.ap.frequency;
    case WidthRole:     return widthOf(row.ap);
    case WidthAssumedRole: return widthIsAssumed(row.ap);
    case DbmRole:       return dbmOf(row.ap.signal);
    case SignalRole:    return row.ap.signal;
    case SecurityRole:  return row.ap.security;
    case RateRole:      return row.ap.maxBitrate;
    case ConnectedRole: return row.ap.connected;
    case ChangedRole:   return int(now - row.lastChange);
    case FirstSeenRole: return int(now - row.firstSeen);
    default:            return QVariant();
    }
}

QHash<int, QByteArray> ApModel::roleNames() const {
    return {
        { SsidRole, "ssid" },
        { BssidRole, "bssid" },
        { BandRole, "band" },
        { ChannelRole, "channel" },
        { FrequencyRole, "frequency" },
        { WidthRole, "width" },
        { WidthAssumedRole, "widthAssumed" },
        { DbmRole, "dbm" },
        { SignalRole, "signal" },
        { SecurityRole, "security" },
        { RateRole, "rate" },
        { ConnectedRole, "connected" },
        { ChangedRole, "changedSeconds" },
        { FirstSeenRole, "firstSeenSeconds" },
    };
}

void ApModel::apply(const QList<AccessPoint> &accessPoints) {
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    QHash<QString, AccessPoint> fresh;
    for (const AccessPoint &ap : accessPoints)
        fresh.insert(ap.bssid.toUpper(), ap);

    bool countMoved = false;

    // Gone from the air (or from NetworkManager's list) means gone from the table.
    for (int row = int(m_rows.size()) - 1; row >= 0; --row) {
        if (fresh.contains(m_rows.at(row).ap.bssid.toUpper()))
            continue;
        beginRemoveRows(QModelIndex(), row, row);
        m_rows.removeAt(row);
        endRemoveRows();
        countMoved = true;
    }

    for (auto it = fresh.constBegin(); it != fresh.constEnd(); ++it) {
        const int row = rowOfBssid(it.key());
        if (row >= 0) {
            Row &existing = m_rows[row];
            if (existing.ap.signal != it->signal)
                existing.lastChange = now;
            existing.ap = it.value();
            emit dataChanged(index(row, 0), index(row, 0));
            continue;
        }

        Row inserted;
        inserted.ap = it.value();
        inserted.firstSeen = now;
        inserted.lastChange = now;
        beginInsertRows(QModelIndex(), int(m_rows.size()), int(m_rows.size()));
        m_rows.append(inserted);
        endInsertRows();
        countMoved = true;
    }

    resort();
    if (countMoved)
        emit countChanged();
}

void ApModel::resort() {
    if (m_rows.size() < 2)
        return;

    QList<Row> sorted = m_rows;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Row &a, const Row &b) {
        if (a.ap.connected != b.ap.connected)
            return a.ap.connected;
        if (a.ap.signal != b.ap.signal)
            return a.ap.signal > b.ap.signal;
        return a.ap.ssid < b.ap.ssid;
    });

    bool unchanged = true;
    for (int row = 0; unchanged && row < sorted.size(); ++row)
        unchanged = sorted.at(row).ap.bssid == m_rows.at(row).ap.bssid;
    if (unchanged)
        return;

    // Reordering in place: tell the view, move the persistent indexes, tell it again.
    emit layoutAboutToBeChanged();

    const QModelIndexList previous = persistentIndexList();
    QList<QString> bssids;
    for (const QModelIndex &index : previous)
        bssids.append(m_rows.at(index.row()).ap.bssid);

    m_rows = sorted;

    QHash<QString, int> position;
    for (int row = 0; row < m_rows.size(); ++row)
        position.insert(m_rows.at(row).ap.bssid, row);

    QModelIndexList updated;
    for (int i = 0; i < previous.size(); ++i) {
        const QModelIndex &old = previous.at(i);
        const int row = position.value(bssids.at(i), old.row());
        updated.append(index(row, old.column(), old.parent()));
    }
    changePersistentIndexList(previous, updated);

    emit layoutChanged();
}
