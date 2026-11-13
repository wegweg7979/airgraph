# Airgraph

A Wi-Fi channel mapper for hurunagus (Fedora bootc + Hyprland + Omarchy): scans
with NetworkManager and draws what is actually on the air — which channels are
crowded, by whom, and how strong.

Planned in `PLAN.md`; nothing here is a released app yet. M0 is three
throwaway spikes under `spikes/`, answering whether the unprivileged
NetworkManager-over-D-Bus data layer is enough to build on:

| # | Spike | Question |
|---|-------|----------|
| 001 | `nm-dbus-scan` | can an unprivileged Qt app read scan results over D-Bus, live and without `iw`? |
| 002 | `six-ghz` | does this AX210 + NetworkManager actually surface 6 GHz access points? |
| 003 | `scan-cadence` | how often will NetworkManager scan, and what arrives for free between scans? |

Every spike is disposable: each `README.md` ends in a verdict, and the code is
not production code.

## Build a spike

```sh
cd spikes/001-nm-dbus-scan
mkdir -p build && cd build
qmake6 ../spike001.pro && make -j"$(nproc)"
./spike001 --watch 20
```

MIT, following Omarchy's own app shape (Omacut/Monologue, © David Heinemeier
Hansson) — attribution lands in `LICENSE` when the app itself starts in M1.
