#include "cli.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace cli {
bool isHeadlessCommand(const QStringList &arguments) {
    if (arguments.size() < 2)
        return false;
    const QString command = arguments.at(1);
    return command == QStringLiteral("scan") || command == QStringLiteral("check")
           || command == QStringLiteral("export") || command == QStringLiteral("help");
}

QString usage() {
    return QStringLiteral(
        "airgraph — see which Wi-Fi channels are crowded, and how strong each access point is\n"
        "\n"
        "  airgraph open                     the window (the default)\n"
        "  airgraph scan [--json]            scan once and print what is on the air\n"
        "  airgraph check [--json]           report whether this machine can be surveyed\n"
        "  airgraph export --csv|--json [path]\n"
        "                                    scan, then write the session to a file\n"
        "                                    (default: ~/Documents/airgraph-<stamp>.<ext>)\n"
        "\n"
        "  --band <2|5|6>   pick the band first\n"
        "  --live <seconds> keep scanning that long before exporting\n"
        "  --demo           made-up access points, for screenshots\n"
        "\n"
        "Read-only: airgraph never connects, disconnects, or changes radio state.\n");
}

QString checkText(const Check &check) {
    QStringList lines;
    if (!check.available) {
        lines << QStringLiteral("cannot survey: %1").arg(check.problem);
        return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }

    lines << QStringLiteral("device: %1").arg(check.device);
    lines << QStringLiteral("network manager: reachable, scanning enabled");
    lines << QStringLiteral("scan request: %1").arg(check.scanAccepted ? QStringLiteral("accepted")
                                                                        : QStringLiteral("refused"));
    lines << QStringLiteral("access points heard: %1").arg(check.accessPoints);
    lines << QStringLiteral("channel plan from: %1").arg(check.phySource);

    for (int gigahertz : { 2, 5, 6 }) {
        const int channels = check.bandChannels.value(gigahertz);
        lines << QStringLiteral("%1 GHz: %2").arg(gigahertz)
                     .arg(channels > 0 ? QStringLiteral("%1 channels this radio can hear").arg(channels)
                                       : QStringLiteral("no such band on this radio"));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString checkJson(const Check &check) {
    QJsonObject root;
    root.insert(QStringLiteral("available"), check.available);
    root.insert(QStringLiteral("device"), check.device);
    root.insert(QStringLiteral("problem"), check.problem);
    root.insert(QStringLiteral("scan_accepted"), check.scanAccepted);
    root.insert(QStringLiteral("access_points"), check.accessPoints);
    root.insert(QStringLiteral("channel_plan_source"), check.phySource);

    QJsonObject bands;
    for (int gigahertz : { 2, 5, 6 })
        bands.insert(QString::number(gigahertz), check.bandChannels.value(gigahertz));
    root.insert(QStringLiteral("band_channels"), bands);

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString scanText(const QList<AccessPoint> &accessPoints, const QString &device) {
    QStringList lines;
    lines << QStringLiteral("device %1 — %2 access points").arg(device).arg(accessPoints.size());
    lines << QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8")
                 .arg(QStringLiteral("SSID"), -22)
                 .arg(QStringLiteral("BSSID"), -18)
                 .arg(QStringLiteral("band"), -5)
                 .arg(QStringLiteral("chan"), -5)
                 .arg(QStringLiteral("width"), -6)
                 .arg(QStringLiteral("dBm"), -7)
                 .arg(QStringLiteral("security"), -9)
                 .arg(QStringLiteral("Mb/s"));

    for (const AccessPoint &ap : accessPoints) {
        // A width marked ~ is the stated 20 MHz assumption, not a measurement —
        // the same convention the window uses.
        const QString width = QStringLiteral("%1%2").arg(widthOf(ap)).arg(widthIsAssumed(ap) ? QStringLiteral("~")
                                                                                             : QString());
        lines << QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8")
                     .arg(ap.ssid.isEmpty() ? QStringLiteral("(hidden)") : ap.ssid, -22)
                     .arg(ap.bssid, -18)
                     .arg(QString::number(bandOf(ap.frequency)), -5)
                     .arg(QString::number(channelOf(ap.frequency)), -5)
                     .arg(width, -6)
                     .arg(QString::number(dbmOf(ap.signal), 'f', 1), -7)
                     .arg(ap.security, -9)
                     .arg(ap.maxBitrate / 1000);
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace cli
