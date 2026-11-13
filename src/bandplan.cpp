#include "bandplan.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStringList>
#include <QThread>

#include <algorithm>

namespace {

// The IEEE channel allocations. These are standards, not measurements, so they
// are the honest fallback when the phy cannot be asked.
QList<int> channelsToFrequencies(const QList<int> &channels) {
    QList<int> frequencies;
    frequencies.reserve(channels.size());
    for (int channel : channels)
        frequencies.append(5000 + channel * 5);   // 5 GHz: channel 36 is 5180 MHz
    return frequencies;
}

} // namespace

BandPlan::BandPlan(QObject *parent) : QObject(parent) {
    for (int gigahertz : { 2, 5, 6 })
        m_frequencies.insert(gigahertz, standard(gigahertz));
}

QList<int> BandPlan::standard(int gigahertz) {
    QList<int> frequencies;
    switch (gigahertz) {
    case 2:
        for (int channel = 1; channel <= 13; ++channel)
            frequencies.append(2407 + channel * 5);   // 2412 … 2472
        frequencies.append(2484);                     // channel 14
        return frequencies;
    case 5:
        return channelsToFrequencies({ 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116,
                                       120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161,
                                       165, 169, 173, 177 });
    case 6:
        for (int frequency = 5955; frequency <= 7115; frequency += 20)
            frequencies.append(frequency);
        return frequencies;
    default:
        return frequencies;
    }
}

QHash<int, QList<int> > BandPlan::parse(const QString &iwPhyInfo) {
    QHash<int, QList<int> > result;
    int gigahertz = 0;
    const QRegularExpression bandExpression(QStringLiteral("^\\s+Band ([0-9]+):"));
    const QRegularExpression frequencyExpression(QStringLiteral("^\\s+\\* ([0-9]+(?:\\.[0-9]+)?) MHz \\["));

    const QStringList lines = iwPhyInfo.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch band = bandExpression.match(line);
        if (band.hasMatch()) {
            // iw calls the bands 1, 2 and 4 for 2.4, 5 and 6 GHz.
            const int number = band.captured(1).toInt();
            gigahertz = number == 1 ? 2 : number == 2 ? 5 : number == 4 ? 6 : 0;
            continue;
        }
        const QRegularExpressionMatch frequency = frequencyExpression.match(line);
        if (gigahertz == 0 || !frequency.hasMatch())
            continue;
        result[gigahertz].append(qRound(frequency.captured(1).toDouble()));
    }

    for (auto it = result.begin(); it != result.end(); ++it)
        std::sort(it->begin(), it->end());
    return result;
}

void BandPlan::load(const QString &device) {
    if (device.isEmpty())
        return;

    auto run = [](const QString &program, const QStringList &arguments) {
        QProcess process;
        process.start(program, arguments);
        if (!process.waitForStarted(1000) || !process.waitForFinished(2000))
            return QString();
        return QString::fromUtf8(process.readAllStandardOutput());
    };

    const QString deviceInfo = run(QStringLiteral("iw"), { QStringLiteral("dev"), device, QStringLiteral("info") });
    if (deviceInfo.isEmpty())
        return;

    const QRegularExpression phyExpression(QStringLiteral("wiphy\\s+([0-9]+)"));
    const QRegularExpressionMatch phy = phyExpression.match(deviceInfo);
    if (!phy.hasMatch())
        return;

    const QString phyName = QStringLiteral("phy%1").arg(phy.captured(1));
    const QString phyInfo = run(QStringLiteral("iw"), { QStringLiteral("phy"), phyName, QStringLiteral("info") });
    if (phyInfo.isEmpty())
        return;

    const QHash<int, QList<int> > parsed = parse(phyInfo);
    if (parsed.isEmpty())
        return;

    // Only replace a band that the phy really reported, so a partial parse
    // cannot silently shrink the axis.
    for (auto it = parsed.cbegin(); it != parsed.cend(); ++it) {
        if (it.value().isEmpty())
            continue;
        m_frequencies.insert(it.key(), it.value());
    }
    m_source = QStringLiteral("iw phy %1").arg(phyName);
}

QList<int> BandPlan::frequencies(int gigahertz) const {
    return m_frequencies.value(gigahertz);
}

bool BandPlan::advertised(int gigahertz) const {
    return !frequencies(gigahertz).isEmpty();
}

// "	channel 100 (5500 MHz), width: 40 MHz, center1: 5510 MHz" — the one line in
// iw's output that gives a real channel width, for the connected link only.
int BandPlan::parseLinkWidth(const QString &iwDevInfo) {
    const QRegularExpression expression(QStringLiteral("width:\\s*([0-9]+)\\s*MHz"));
    const QRegularExpressionMatch match = expression.match(iwDevInfo);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

int BandPlan::parseLinkFrequency(const QString &iwDevInfo) {
    const QRegularExpression expression(QStringLiteral("channel\\s+[0-9]+\\s*\\(([0-9]+)\\s*MHz\\)"));
    const QRegularExpressionMatch match = expression.match(iwDevInfo);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

int BandPlan::parseLinkCenterFrequency(const QString &iwDevInfo) {
    const QRegularExpression expression(QStringLiteral("center1:\\s*([0-9]+)\\s*MHz"));
    const QRegularExpressionMatch match = expression.match(iwDevInfo);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

QString BandPlan::parseLinkSsid(const QString &iwDevInfo) {
    const QRegularExpression expression(QStringLiteral("^\\s*ssid\\s+(.+)$"),
                                        QRegularExpression::MultilineOption);
    const QRegularExpressionMatch match = expression.match(iwDevInfo);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

int BandPlan::currentLinkWidth(const QString &device) {
    if (device.isEmpty())
        return 0;
    QProcess process;
    process.start(QStringLiteral("iw"), { QStringLiteral("dev"), device, QStringLiteral("info") });
    if (!process.waitForStarted(1000) || !process.waitForFinished(2000))
        return 0;
    return parseLinkWidth(QString::fromUtf8(process.readAllStandardOutput()));
}
