#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>

#include "wifisource.h"

// The access points, keyed by BSSID: one network on two bands is two rows, and
// an AP that stops answering is dropped rather than frozen on screen.
class ApModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        SsidRole = Qt::UserRole + 1,
        BssidRole,
        BandRole,
        ChannelRole,
        FrequencyRole,
        WidthRole,
        DbmRole,
        SignalRole,
        SecurityRole,
        RateRole,
        ConnectedRole,
        ChangedRole,     // seconds since this AP's signal last moved
        FirstSeenRole,   // seconds since this session first heard it
    };
    Q_ENUM(Role)

    explicit ApModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }

    // Merge a fresh read: new BSSIDs are inserted, known ones updated in place,
    // vanished ones removed, first-seen times kept. Strongest first, and the AP
    // the machine is associated with ahead of everything.
    void apply(const QList<AccessPoint> &accessPoints);

signals:
    void countChanged();

private:
    struct Row {
        AccessPoint ap;
        qint64 firstSeen = 0;
        qint64 lastChange = 0;
    };

    int rowOfBssid(const QString &bssid) const;
    void resort();

    QList<Row> m_rows;
};
