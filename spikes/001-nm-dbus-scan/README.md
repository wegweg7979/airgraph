# Spike 001 — unprivileged Wi-Fi scan out of NetworkManager over D-Bus

## Question

Given a Qt app with no privileges, when it calls NetworkManager over the system
bus, then it gets the access-point list (SSID, BSSID, frequency, strength,
security) — and it sees live updates without asking for a scan.

Kills the whole design if false: `iw dev wlp4s0 scan` is *Operation not
permitted* for a normal user on this host, and there is no passwordless sudo,
so every v1 feature depends on NetworkManager being the unprivileged door in.

## Approach

Plain Qt 6 console app, `QT += dbus`, one file. No nmcli parsing anywhere: if
this app has to be built out of string scraping, the design changes.

- `NetworkManager.GetDevices` → find `DeviceType == 2` (Wi-Fi)
- `Device.Wireless.RequestScan({})`, timed, error name and message printed
- `GetAccessPoints()` (falls back to the deprecated-but-present
  `AccessPoints` property) → `Properties.GetAll` per AP
- then, with **no further scan requests**: subscribe to `AccessPointAdded`,
  `AccessPointRemoved`, and per-AP `PropertiesChanged` and print what arrives

## Run

```
cd spikes/001-nm-dbus-scan && mkdir -p build && cd build
qmake6 ../spike001.pro && make -j"$(nproc)"
./spike001 --watch 30 --rescan 8     # listen 30 s, one scan at 8 s to prove the wiring
```

`--rescan N` exists because silence has two meanings: nothing was pushed, or
nothing was connected. One deliberate scan mid-listen separates them.

## Results

See `results.txt` (raw terminal output, captured 2026-11-12).

## Verdict: VALIDATED

### What worked

- `RequestScan` accepted with no polkit prompt, 2 ms for the call itself, on a
  normal user account with no `CAP_NET_ADMIN`.
- `GetAccessPoints` answered and every field the app needs is present:
  `Ssid` (ay), `HwAddress`, `Frequency`, `MaxBitrate`, `Strength`,
  `Flags`/`WpaFlags`/`RsnFlags`, `LastSeen`.
- Live signals fire on their own: `PropertiesChanged` on AP paths, delivered
  without the process asking for anything (see results).
- The D-Bus variant nesting is the only wrinkle: `GetAll` hands back values
  wrapped in `QDBusVariant` and the deprecated `AccessPoints` property comes
  back as a `QDBusArgument` — one `unwrap()` helper handles both.

### What didn't

- `Strength` is NM's 0–100 scale, not dBm. dBm has to be derived
  (`strength / 2 - 100`, NM's own inverse mapping) and labelled as derived —
  it is a 1 dB-quantised approximation, fine for a graph axis, not for
  measurement claims.
- No channel width anywhere in NM's data (expected: that is what the deferred
  `iw` path would add).
- `LastSeen` is a CLOCK_BOOTTIME millisecond timestamp, not an age.

### Surprises

- **`RequestScan` returns in 2 ms, the results land ~4.5 s later.** A request at
  7830 ms produced a burst of `PropertiesChanged` on every AP path at
  12381 ms. So a UI must *listen*, never sleep-and-read: the old
  `msleep(6000)` in this spike is the wrong shape.
- **NM pushes nothing when nothing changed.** The first 25 s run — a stable
  signal — delivered *zero* events. The second run saw the connected AP
  announce `Strength` at 814 ms, 12813 ms and 24816 ms: roughly every 12 s,
  which is the driver's own roam/background scan refreshing the *connected*
  BSS only. Passive listening alone is therefore not enough for a live graph.
- Two `hurunagus` BSSIDs (2.4 and 5 GHz) and a `VM7074731` pair (2.4 and 5) —
  the same network appearing on several bands is normal, so dedupe is by
  BSSID, never by SSID, and one SSID may legitimately occupy several channels.
- An open network was in range (`EE WiFi`), so the security column has to be
  real data, not a "probably WPA2" assumption.
- Ten APs on one scan, six on another, five frequencies one run and six the
  next: the air is not a fixed set, and the app's layout must survive it.

### Recommendation for the real build

- NetworkManager over QtDBus is the v1 data layer, confirmed. No `nmcli`
  parsing, no root, no polkit.
- Keep the `unwrap()` helper and the `GetAccessPoints`-then-property fallback
  in `scanbackend`.
- Drive the UI from signals (`AccessPointAdded`/`Removed`, per-AP
  `PropertiesChanged`), and treat a scan request as "ask, then wait for the
  push" — not as a synchronous read.
- Model history keyed by BSSID; NM's `Strength` is the only live signal source,
  and it is 1 dB-quantised, so the graph's y-axis must not pretend to be a
  measurement instrument.
- Draw 20 MHz as the assumed channel width (NM does not report width) and
  label it as assumed until the deferred `iw` path exists.
- Show per-band AP counts honestly: 6 GHz can legitimately be empty.
