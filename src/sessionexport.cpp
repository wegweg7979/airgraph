#include "sessionexport.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace sessionexport {

namespace {

// RFC 4180: quote when the field contains a comma, a quote or a newline, and
// double any quote inside. An SSID really can contain all three.
QString csvField(const QString &value) {
    const bool needsQuotes = value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"'))
                             || value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'));
    if (!needsQuotes)
        return value;
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString bandName(int frequency) {
    switch (bandOf(frequency)) {
    case 5:  return QStringLiteral("5");
    case 6:  return QStringLiteral("6");
    default: return QStringLiteral("2.4");
    }
}

} // namespace

QString toCsv(const QList<AccessPoint> &accessPoints, const ApHistory &history, const QString &device) {
    QStringList lines;
    lines << QStringLiteral("# airgraph session, device %1, %2").arg(device, QDateTime::currentDateTime().toString(Qt::ISODate));
    lines << QStringLiteral("bssid,ssid,band,channel,frequency_mhz,width_mhz,width_assumed,security,"
                            "max_bitrate_kbps,connected,seconds,dbm");

    for (const AccessPoint &ap : accessPoints) {
        const QString prefix = QStringList{
            csvField(ap.bssid), csvField(ap.ssid), bandName(ap.frequency), QString::number(channelOf(ap.frequency)),
            QString::number(ap.frequency), QString::number(widthOf(ap)), widthIsAssumed(ap) ? QStringLiteral("yes") : QStringLiteral("no"),
            csvField(ap.security), QString::number(ap.maxBitrate), ap.connected ? QStringLiteral("yes") : QStringLiteral("no")
        }.join(QLatin1Char(','));

        const QList<SignalSample> series = history.samples(ap.bssid);
        if (series.isEmpty()) {
            // An AP with no samples yet is still worth a row: the table showed it.
            lines << prefix + QStringLiteral(",,%1").arg(dbmOf(ap.signal), 0, 'f', 2);
            continue;
        }
        for (const SignalSample &sample : series)
            lines << prefix + QStringLiteral(",%1,%2").arg(sample.seconds).arg(sample.dbm, 0, 'f', 2);
    }

    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString toJson(const QList<AccessPoint> &accessPoints, const ApHistory &history, const QString &device,
               bool includeSeries) {
    QJsonObject root;
    root.insert(QStringLiteral("generated"), QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("device"), device);
    root.insert(QStringLiteral("scan_interval_seconds"), 10);
    root.insert(QStringLiteral("notes"), QStringLiteral(
        "dBm is derived from NetworkManager's 0-100 strength (signal/2-100). "
        "Channel width is measured only for the associated link (iw dev) and assumed 20 MHz otherwise, "
        "which width_assumed marks per access point."));

    QJsonArray array;
    for (const AccessPoint &ap : accessPoints) {
        QJsonObject object;
        object.insert(QStringLiteral("bssid"), ap.bssid);
        object.insert(QStringLiteral("ssid"), ap.ssid);
        object.insert(QStringLiteral("band_ghz"), bandOf(ap.frequency));
        object.insert(QStringLiteral("channel"), channelOf(ap.frequency));
        object.insert(QStringLiteral("frequency_mhz"), ap.frequency);
        object.insert(QStringLiteral("width_mhz"), widthOf(ap));
        object.insert(QStringLiteral("width_assumed"), widthIsAssumed(ap));
        object.insert(QStringLiteral("dbm"), dbmOf(ap.signal));
        object.insert(QStringLiteral("strength"), ap.signal);
        object.insert(QStringLiteral("security"), ap.security);
        object.insert(QStringLiteral("max_bitrate_kbps"), double(ap.maxBitrate));
        object.insert(QStringLiteral("connected"), ap.connected);

        QJsonArray series;
        if (includeSeries) {
            for (const SignalSample &sample : history.samples(ap.bssid)) {
                QJsonObject point;
                point.insert(QStringLiteral("seconds"), double(sample.seconds));
                point.insert(QStringLiteral("dbm"), sample.dbm);
                series.append(point);
            }
            object.insert(QStringLiteral("samples"), series);
        }
        array.append(object);
    }
    root.insert(QStringLiteral("access_points"), array);

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString suggestedFileName(const QString &extension) {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    return QStringLiteral("airgraph-%1.%2").arg(stamp, extension);
}

} // namespace sessionexport
