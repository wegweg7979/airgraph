import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Airgraph 1.0

ApplicationWindow {
    id: win
    width: 1020
    height: 780
    minimumWidth: 720
    minimumHeight: 520
    visible: true
    title: qsTr("Airgraph")
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    // Column widths live here so the header and the rows cannot drift apart.
    readonly property int colSsid: 210
    readonly property int colBssid: 150
    readonly property int colFrequency: 64
    readonly property int colChannel: 110
    readonly property int colDbm: 70
    readonly property int colPercent: 56
    readonly property int colSecurity: 74
    readonly property int colRate: 88
    readonly property int colChanged: 78
    readonly property color textColor: "#d8d8d8"
    readonly property color dimColor: "#8a8a8a"

    property bool helpVisible: false
    property bool showHistory: true

    // One band at a time, the way a Wi-Fi analyzer works: the graph, the
    // congestion strip and the table all show the same one.
    readonly property string bandLabel: backend.gigahertz === 2 ? qsTr("2.4 GHz")
                                       : backend.gigahertz === 5 ? qsTr("5 GHz") : qsTr("6 GHz")

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: backend.live = !backend.live }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; onActivated: backend.rescan() }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; onActivated: win.showHistory = !win.showHistory }
    Shortcut { sequence: "E"; context: Qt.ApplicationShortcut; onActivated: backend.exportSessionCsv() }
    Shortcut { sequence: "Shift+E"; context: Qt.ApplicationShortcut; onActivated: backend.exportSessionJson() }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; onActivated: backend.gigahertz = 2 }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; onActivated: backend.gigahertz = 5 }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; onActivated: backend.gigahertz = 6 }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; onActivated: list.incrementCurrentIndex() }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; onActivated: list.decrementCurrentIndex() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    text: qsTr("Airgraph")
                    color: theme.accent
                    font.pixelSize: 20
                    font.bold: true
                }
                Text {
                    text: backend.device.length > 0 ? backend.device : qsTr("no device")
                    color: win.dimColor
                    font.pixelSize: 13
                }
                Item { Layout.fillWidth: true }
                Text {
                    // "no data" only when there is genuinely nothing; demo mode
                    // has rows and no radio, and must not read as a failure.
                    text: backend.available
                          ? (backend.waitingForScan
                             ? qsTr("scanning…")
                             : qsTr("data %1 s old").arg(backend.secondsSinceScan))
                          : (backend.accessPoints.count > 0 ? qsTr("demo data") : qsTr("no data"))
                    color: win.dimColor
                    font.pixelSize: 13
                }
                Text {
                    text: backend.live ? qsTr("live · every %1 s").arg(backend.scanIntervalSeconds)
                                       : qsTr("paused")
                    color: backend.live ? theme.accent : win.dimColor
                    font.pixelSize: 13
                    font.bold: backend.live
                }
            }

            // A blocker is reported in the window, never worked around quietly.
            Rectangle {
                Layout.fillWidth: true
                visible: backend.problem.length > 0
                implicitHeight: problemText.implicitHeight + 16
                color: "#2a1416"
                radius: Math.min(6, theme.radius)
                border.color: "#7a2b30"
                Text {
                    id: problemText
                    anchors.fill: parent
                    anchors.margins: 8
                    text: backend.problem
                    color: "#f2b8b8"
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                }
            }

            Text {
                Layout.fillWidth: true
                visible: backend.scanError.length > 0
                text: qsTr("Last scan request was refused: %1").arg(backend.scanError)
                color: "#e0a96d"
                wrapMode: Text.WordWrap
                font.pixelSize: 13
            }

            // Band tabs, with the count on each so an empty band reads as empty
            // rather than broken.
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Repeater {
                    model: [ { "gigahertz": 2, "label": qsTr("2.4 GHz") },
                             { "gigahertz": 5, "label": qsTr("5 GHz") },
                             { "gigahertz": 6, "label": qsTr("6 GHz") } ]

                    delegate: Rectangle {
                        required property var model
                        implicitWidth: tabText.implicitWidth + 28
                        implicitHeight: 26
                        radius: Math.min(6, theme.radius)
                        color: backend.gigahertz === model.gigahertz ? theme.accent : "#1c1c21"

                        Text {
                            id: tabText
                            anchors.centerIn: parent
                            text: model.label + "  " + (backend.accessPoints.countsByBand[String(model.gigahertz)] || 0)
                            color: backend.gigahertz === model.gigahertz ? theme.accentForeground : win.dimColor
                            font.pixelSize: 12
                            font.bold: backend.gigahertz === model.gigahertz
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: backend.gigahertz = model.gigahertz
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }

            // The graph: one bell per access point across a linear frequency
            // axis, with the per-channel congestion strip underneath.
            SpectrumView {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                model: backend.accessPoints
                bandPlan: bandPlan
                gigahertz: backend.gigahertz
                accent: theme.accent
                radius: theme.radius
            }

            // Signal over time for the selected access point: the raw samples as
            // dots and the rolling mean as the line.
            HistoryView {
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 120 : 0
                visible: win.showHistory
                history: backend.history
                model: backend.accessPoints
                bssid: backend.selectedBssid
                accent: theme.accent
                radius: theme.radius
            }

            Text {
                text: qsTr("%n access point(s)", "", backend.bandAccessPoints.count) + " · " + win.bandLabel
                color: win.textColor
                font.pixelSize: 13
                font.bold: true
            }

            // Header row
            Row {
                Layout.fillWidth: true
                spacing: 8
                Text { text: qsTr("SSID");       color: win.dimColor; font.pixelSize: 12; width: win.colSsid }
                Text { text: qsTr("BSSID");      color: win.dimColor; font.pixelSize: 12; width: win.colBssid }
                Text { text: qsTr("MHz");        color: win.dimColor; font.pixelSize: 12; width: win.colFrequency }
                Text { text: qsTr("channel · width"); color: win.dimColor; font.pixelSize: 12; width: win.colChannel }
                Text { text: qsTr("dBm");        color: win.dimColor; font.pixelSize: 12; width: win.colDbm }
                Text { text: qsTr("%");          color: win.dimColor; font.pixelSize: 12; width: win.colPercent }
                Text { text: qsTr("security");   color: win.dimColor; font.pixelSize: 12; width: win.colSecurity }
                Text { text: qsTr("Mb/s");       color: win.dimColor; font.pixelSize: 12; width: win.colRate }
                Text { text: qsTr("changed");    color: win.dimColor; font.pixelSize: 12; width: win.colChanged }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: "#2b2b30"
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: backend.bandAccessPoints
                currentIndex: -1

                onCurrentIndexChanged: {
                    const item = currentItem
                    if (item && item.model)
                        backend.selectedBssid = item.model.bssid
                }

                Text {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: backend.problem.length > 0
                          ? qsTr("Nothing to show until Wi-Fi can be scanned.")
                          : (backend.waitingForScan ? qsTr("Scanning…") : qsTr("No access points heard yet. Press R."))
                    color: win.dimColor
                    font.pixelSize: 14
                }

                delegate: Rectangle {
                    required property var model
                    required property int index
                    width: list.width
                    height: 24
                    color: model.connected ? Qt.rgba(1, 1, 1, 0.06)
                                           : (index === list.currentIndex ? Qt.rgba(1, 1, 1, 0.05) : "transparent")

                    // Whatever the keyboard lands on is what the time graph shows;
                    // the ListView reports the change, so the delegate itself
                    // needs no binding that could fire before model is ready.
                    MouseArea {
                        anchors.fill: parent
                        onClicked: list.currentIndex = index
                    }

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        Text {
                            text: model.connected ? model.ssid + "  ●" : (model.ssid.length > 0 ? model.ssid : qsTr("(hidden)"))
                            color: model.connected ? theme.accent : win.textColor
                            font.pixelSize: 13
                            font.bold: model.connected
                            width: win.colSsid
                            elide: Text.ElideRight
                        }
                        Text { text: model.bssid; color: win.dimColor; font.pixelSize: 13; width: win.colBssid }
                        Text { text: model.frequency; color: win.textColor; font.pixelSize: 13; width: win.colFrequency }
                        Text {
                            // The width is measured for your own link and
                            // assumed (marked ~) for everyone else's.
                            text: model.channel + " · " + model.width + (model.widthAssumed ? "~" : "")
                            color: win.textColor; font.pixelSize: 13; width: win.colChannel
                        }
                        Text {
                            text: model.dbm.toFixed(1)
                            color: model.dbm > -67 ? theme.accent : win.textColor
                            font.pixelSize: 13; width: win.colDbm
                        }
                        Text { text: model.signal; color: win.dimColor; font.pixelSize: 13; width: win.colPercent }
                        Text { text: model.security; color: win.textColor; font.pixelSize: 13; width: win.colSecurity }
                        Text {
                            // NetworkManager reports the maximum bitrate in kbit/s.
                            text: (model.rate / 1000).toFixed(0)
                            color: win.dimColor; font.pixelSize: 13; width: win.colRate
                        }
                        Text { text: model.changedSeconds + " s"; color: win.dimColor; font.pixelSize: 13; width: win.colChanged }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Space live · 1/2/3 band · R rescan · T time graph · E export csv · Shift+E json · ? help · Q quit · ~ width assumed")
                    color: win.dimColor
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Text {
                    // Where the export went, so it is never a mystery file.
                    visible: backend.lastExportPath.length > 0
                    text: qsTr("exported → %1").arg(backend.lastExportPath)
                    color: theme.accent
                    font.pixelSize: 12
                    elide: Text.ElideLeft
                    Layout.maximumWidth: win.width * 0.5
                }
            }
        }

        // Shortcut overlay
        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: Qt.rgba(0, 0, 0, 0.72)

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 60, 460)
                implicitHeight: helpColumn.implicitHeight + 32
                color: "#17171a"
                radius: Math.min(8, theme.radius)
                border.color: "#2b2b30"

                Column {
                    id: helpColumn
                    anchors.centerIn: parent
                    width: parent.width - 32
                    spacing: 8

                    Text { text: qsTr("Shortcuts"); color: theme.accent; font.pixelSize: 16; font.bold: true }
                    Repeater {
                        model: [
                            { "keys": "Space", "what": qsTr("live scanning on and off") },
                            { "keys": "1/2/3", "what": qsTr("2.4, 5, or 6 GHz") },
                            { "keys": "R",     "what": qsTr("scan once, now") },
                            { "keys": "T",     "what": qsTr("signal-over-time graph") },
                            { "keys": "E",     "what": qsTr("export the session as CSV") },
                            { "keys": "Shift+E", "what": qsTr("export as JSON") },
                            { "keys": "↑ / ↓", "what": qsTr("move down the list, which picks the time graph") },
                            { "keys": "?",     "what": qsTr("this list") },
                            { "keys": "Q",     "what": qsTr("quit") }
                        ]
                        delegate: Row {
                            required property var model
                            spacing: 12
                            Text { text: model.keys; color: win.textColor; font.pixelSize: 13; font.bold: true; width: 80 }
                            Text { text: model.what; color: win.textColor; font.pixelSize: 13 }
                        }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: win.helpVisible = false
            }
        }
    }
}
