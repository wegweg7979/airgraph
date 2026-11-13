# Airgraph

See which Wi-Fi channels are crowded, and how strong each access point is: a
channel graph in the shape of the Wi-Fi Analyzer app, plus the raw table behind
it and the signal over time.

![airgraph showing the 2.4 GHz channel graph, the congestion strip, the signal-over-time panel and the access point table](assets/screenshot.png)

*(The screenshot above is `--demo`: made-up access points, so no real network
name is published.)*

Scans with NetworkManager over D-Bus — no root, no polkit prompt — and draws
what is on the air. It never connects, disconnects, or changes radio or
regulatory state: it reads the air and reports it.


airgraph check                 # can this machine be surveyed? device, bands, scan accepted
airgraph scan                  # one scan, printed as a table
airgraph scan --json           # the same, as JSON
airgraph export --csv           # scan, then write ~/Documents/airgraph-<stamp>.csv
airgraph export --json --live 60  # keep scanning for a minute first: the series comes too
airgraph help
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



## Fedora package (COPR)

```sh
sudo dnf copr enable wegweg7979/airgraph
sudo dnf install airgraph
```



## Licence

MIT. The window shape, theme follower and translator follow Omarchy's own apps
(Omacut/Monologue, MIT © David Heinemeier Hansson) — see `LICENSE`.
