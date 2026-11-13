#pragma once

#include <QList>
#include <QString>

#include "aphistory.h"
#include "wifisource.h"

// The session as a file, built as a string first so the tests can read exactly
// what would be written.
//
// CSV is long format — one row per sample with the access point's details
// repeated on each — because that is what a spreadsheet or a script can
// actually use. JSON keeps the full snapshot plus each series.
namespace sessionexport {

QString toCsv(const QList<AccessPoint> &accessPoints, const ApHistory &history, const QString &device);
QString toJson(const QList<AccessPoint> &accessPoints, const ApHistory &history, const QString &device);

// airgraph-20261113-142530.csv — sortable, and never overwrites a previous run.
QString suggestedFileName(const QString &extension);

} // namespace sessionexport
