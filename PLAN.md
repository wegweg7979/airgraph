# Airgraph — plan

A Wi-Fi channel mapper for hurunagus. Scans with NetworkManager and draws what
is actually on the air: which channels are crowded, by whom, and how strong.

**Decisions taken (all defaults accepted, 2026-11-12):**

| Question | Decision |
|---|---|
| "Map" | Spectrum/channel map only. No floor plan — one adapter and no GPS means any floor plan would be a drawing, not a measurement. |
| Location | `~/Work/airgraph` |
| Privileged data | NetworkManager only in v1. No polkit prompts. |
| Name | `airgraph` (binary, package, `.desktop`, Wayland app id) |
| Packaging | User-local install in v1; RPM + COPR + image at M5 (user's side of the fence) |

## Ground truth on this box (measured, not assumed)

- Intel AX210 (Wi-Fi 6E), `wlp4s0`. phy advertises Band 1 (14 ch, 2412–2484),
  Band 2 (37 ch, 5180–5905), Band 4 (59 ch, 5955–7115).
- NetworkManager 1.58.1, wpa_supplicant 2.11, regulatory country GB
  (6 GHz 5925–6425 allowed), phy self-managed (`country DE`).
- `nmcli dev wifi list --rescan yes` works unprivileged in ~4.5 s, no prompt.
- `iw dev wlp4s0 scan` and `iw … survey dump` are **Operation not permitted**
  for a normal user, and there is no passwordless sudo. Privileged data would
  need a `pkexec` helper — deliberately out of v1.
- Qt 6.11.2 with base/declarative/svg/wayland/multimedia/test and QtDBus
  headers present; `qmake6`, `g++`, `make` present. Nothing to install.
- No wifi GUI or monitor installed (wavemon, linssid, iwgtk, nmtui, nm-applet).
- `/usr/local/bin` is root-owned on this immutable host → v1 installs to
  `~/.local/bin` + `~/.local/share/applications`.

## Data layer (validated by M0)

NetworkManager over QtDBus, `Device.Wireless.RequestScan` +
`GetAccessPoints`, live `AccessPointAdded`/`Removed`/`PropertiesChanged`.

- Scan cadence: **one request every ~10 s**, plus manual rescan. NM accepts
  every request at 2 s and silently coalesces instead of erroring, so a faster
  poll is fiction (spike 003).
- Results arrive ~4.5–12 s after a request, as signals. The UI renders on
  signal arrival; there is no synchronous read.
- `Strength` is NM's 0–100, converted for display as `strength / 2 - 100` and
  labelled as derived.
- Channel width is not in NM's data → draw 20 MHz and mark it assumed.
- Read-only, always: never connect, disconnect, or change radio/regulatory
  state.

## Views

1. **Spectrum map** — x = frequency for the selected band, y = signal
   (−95…−30 dBm), one arc per BSSID spanning its assumed width, the connected
   AP marked distinctly.
2. **Occupancy strip + recommendation** — per-channel overlap-weighted score,
   the 2.4 GHz 1/6/11 plans scored explicitly, DFS channels flagged on 5 GHz.
3. **AP table** — SSID / BSSID / band / chan / width / dBm / % / security /
   max rate / age. Deduped by BSSID.
4. **Live history** — ring buffer per BSSID with a rolling sparkline (the raw
   signal swings several dB while stationary, so smoothing is required), plus
   CSV/JSON session export.

The 6 GHz band is a first-class view that is allowed to be empty ("no 6 GHz APs
heard"), with the phy's capability shown as a separate fact.

## Shape

```
airgraph.pro          qmake6: quick, dbus, svg, test
bin/build  bin/test  bin/install      (user-local install)
src/main.cpp          QGuiApplication, setDesktopFileName, context properties
src/scanbackend.*     NM D-Bus client: scan cadence, signals, unwrap helpers
src/apmodel.*         BSSID dedupe, band/width, dBm conversion, history rings
src/spectrumitem.*    QQuickPaintedItem — one painter for screen and PNG export
src/theme.*           Omarchy accent (colors.toml) + hyprctl rounding
src/translator.*      Omarchy language, live, per the omarchy-app skill
src/Main.qml          layout and every key as a Shortcut
tests/airgraph_tests.{pro,cpp}   offscreen, fake scanner injected
```

Keyboard: `Space` live/stop · `R` rescan · `1/2/3` band · arrows/`Tab` APs ·
`E` export · `?` shortcuts · `Q` quit.

Agent CLI (Hype's pattern): `airgraph scan --json`, `airgraph export --csv`,
`airgraph check` (no Wi-Fi device, NM unreachable, band not scannable) —
`QT_QPA_PLATFORM=offscreen`, exit 0/1, errors on stderr, GUI behind
`airgraph open`.

## Milestones

- **M0** spikes 001–003 (done, see `spikes/`).
- **M1** skeleton: build/run loop, theme, window, live AP table from a real scan.
- **M2** spectrum map + occupancy math + best-channel recommendation.
- **M3** live mode (10 s cadence + signals), history sparklines, CSV/JSON export.
- **M4** agent CLI, offscreen tests, translations.
- **M5** RPM spec + COPR + image handoff (user builds/pushes), README screenshot.

## Packaging

v1: no root, nothing touching the immutable OS — `~/.local/bin/airgraph` plus a
`.desktop` entry and icon. M5: Fedora spec in the omedora COPR (per the
quickshell skill's Fedora path) and the package baked into `hurunagus-hyprland`,
drafted here and built/pushed by the user.
