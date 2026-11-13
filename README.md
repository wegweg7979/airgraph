# Airgraph

See which Wi-Fi channels are crowded, and how strong each access point is: a
channel graph in the shape of the Wi-Fi Analyzer app, plus the raw table behind
it.

Scans with NetworkManager over D-Bus — no root, no polkit prompt — and draws
what is on the air. It never connects, disconnects, or changes radio or
regulatory state: it reads the air and reports it.

> Status: the graph (M2), the live table and the scan cadence (M1) are done.
> `PLAN.md` has the milestones; `spikes/` has the three M0 experiments that
> shaped the design.

## Run it

```sh
bin/build && ./build/airgraph
```

## Keys

| Key | What it does |
|-----|--------------|
| `Space` | live scanning on and off (one scan every 10 s) |
| `1` `2` `3` | 2.4 GHz, 5 GHz, 6 GHz — the graph and the table follow |
| `T` | signal-over-time graph on and off |
| `E` | export the session as CSV |
| `Shift+E` | export the session as JSON |
| `R` | scan once, now |
| `↑` `↓` | move down the list — the selection is what the time graph follows |
| `?` | the shortcut list |
| `Q` | quit |

## Signal over time

The panel under the graph follows the selected access point (the one you are
associated with, until you move the selection). It draws the raw samples as dots
and a **rolling mean** as the line, because the raw signal moves several dB while
the machine sits perfectly still — spike 003 measured −65.5 → −55.0 dBm inside
20 s at a desk. The dBm range follows the data with a 20 dB floor on the span, and
the axis labels state where the window actually sits.

## Export

`E` writes **CSV** (long format: one row per sample with the AP's details
repeated, so a spreadsheet or script can use it directly), `Shift+E` writes
**JSON** (the full snapshot plus every series). Both land in `~/Documents` as
`airgraph-<date>-<time>.csv|json`, written beside the target and renamed so an
interrupted export cannot truncate a previous one. The window shows the path it
wrote.

## The graph

One bell per access point across a **linear frequency axis** — that is what makes
2.4 GHz's overlapping channels honest and 5 GHz's gaps visible — with a
per-channel congestion strip underneath showing a bar for how busy each channel
is and a five-dot rating (`●●●●●` clear, `●○○○○` swamped). The summary line names
the cleanest of 1/6/11 on 2.4 GHz, the only three that do not overlap each other.
The window opens on the band your machine is associated on, and the tabs carry
per-band counts, so an empty 6 GHz reads as empty rather than broken.

The axis comes from this radio, not a hardcoded table: `iw phy <phy> info` lists
the channels the phy actually has (no privilege needed), with the IEEE table as
a fallback.

## Install (this machine, hurunagus)

```sh
bin/install          # ~/.local/bin/airgraph, plus the launcher entry and icon
```

`/usr` is read-only on this immutable host, so the install goes to `~/.local`
and touches nothing else. The RPM (M5) is the system-wide path.

## What the numbers mean

- **dBm** is derived, not measured: NetworkManager reports strength as 0–100,
  and this inverts its own mapping (`strength / 2 − 100`), quantised to 0.5 dB.
  Good for an axis, not for a measurement.
- **`channel · width`** — width is measured for the link your machine is on
  (`iw dev <iface> info` reports it, unprivileged: 40 MHz here) and **assumed
  20 MHz** for every other AP, which is marked with `~`. NetworkManager itself
  reports no width at all, and reading other APs' widths needs `iw scan`, i.e.
  root.
- **changed** — seconds since this access point's signal last moved.
- **One scan every 10 s** is deliberate: NetworkManager accepts faster requests
  and silently coalesces them, so a faster poll shows no more data (spike 003
  measured this). Data age is displayed honestly, and "scanning…" only clears
  when `LastScan` proves a scan really completed.

## Tests

```sh
bin/test        # offscreen, no display, no D-Bus, no scanning
```

Every number the graph draws — the frequency axis, the dBm axis, the bell, the
congestion score, the 1/6/11 choice — is a pure function in `src/spectrum.*` and
is pinned by a test. That is not ceremony: the dBm axis sign was wrong the first
time, and the test is what caught it.

## Screenshot without a display

```sh
QT_QPA_PLATFORM=offscreen ./build/airgraph --band 2 --grab /tmp/airgraph.png
```

## Licence

MIT. The window shape, theme follower and translator follow Omarchy's own apps
(Omacut/Monologue, MIT © David Heinemeier Hansson) — see `LICENSE`.
