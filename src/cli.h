#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "wifisource.h"

// The commands that need no display, in the shape Omarchy's own apps use:
// each takes --json, exits 0 or 1 with the errors on stderr, and never opens a
// window. The GUI is behind `airgraph open`, which is also the default.
namespace cli {

struct Check {
    QString device;
    QString problem;            // empty when healthy
    QString phySource;          // "iw phy phy0" or the standard table
    bool available = false;
    bool scanAccepted = false;
    QHash<int, int> bandChannels;   // 2 / 5 / 6 GHz → channels this radio has
    int accessPoints = 0;
};

bool isHeadlessCommand(const QStringList &arguments);
QString usage();

// Both builders are pure, so the tests can pin the output without a bus.
QString checkText(const Check &check);
QString checkJson(const Check &check);

// The snapshot as a table a terminal can read, the same fields the window shows.
QString scanText(const QList<AccessPoint> &accessPoints, const QString &device);

} // namespace cli
