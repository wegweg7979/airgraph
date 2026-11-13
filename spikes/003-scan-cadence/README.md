# Spike 003 — scan cadence: how often will NetworkManager actually scan?

## Question

Given a live graph that wants fresh data continuously, when we call
`RequestScan` faster than the device allows, then NetworkManager refuses with
a recognisable error — and we know the floor, plus how much a passive listener
sees for free.

This decides the whole live-mode design: poll interval, or signals, or both.

## Approach

`spike003 <interval-seconds> <attempts> [passive-seconds]`

- calls `RequestScan` every *interval* seconds for *attempts* attempts,
  printing the D-Bus error name and message whenever it is refused
- reads the device's `LastScan` property (monotonic seconds, set when a scan
  completes) before and after each attempt, so an accepted-but-not-run scan is
  visible as a `LastScan` that did not move
- prints access-point count and strongest strength each attempt, to show
  whether anything actually changed
- then listens for *passive-seconds* with no scan requests at all, counting
  `PropertiesChanged` updates on AP strengths — the free update rate that a
  live view can rely on between scans

## Run

```
spike003 2 6 20      # every 2 s, 6 times, then 20 s of passive listening
```

## Results

See `results.txt`.

## Verdict: VALIDATED

### What worked

- Six `RequestScan` calls, 2 s apart, **all accepted** — 1–6 ms each, no polkit
  prompt, no error.
- The passive window proved the free update path: 10 strength updates in 20 s
  with zero scans requested. Nine arrived in a single burst 7.4 s after the
  last completed scan; the tenth at 10.7 s was the connected AP refreshing
  itself on its own.

### What didn't

- **Acceptance is not a scan.** Only 2 of 6 requests actually completed a scan
  — NetworkManager silently coalesces instead of refusing, so a fast poll buys
  nothing but wastes airtime and battery. `LastScan` is the only way to tell an
  accepted request from a performed one.
- `LastScan` is monotonic but its unit is not what the documentation says
  (deltas of 11669 and 7577 for events seconds apart, against an uptime of
  1 day 17 h). Treat the *delta* as the signal and the value as opaque.

### Surprises

- The refusal case this spike was looking for **does not exist at these
  intervals** — the error name and message printing never fired once. The
  design cannot rely on being told "too soon"; it has to pace itself.
- A full scan completes ~4.5–12 s after the request, and everything the UI
  needs arrives as signals at that moment, not as a return value.
- Ten APs throughout, but the strongest reported strength swung between
  −65.5 and −55.0 dBm within 20 s with the machine stationary — so any
  "signal over time" view needs smoothing or a rolling window, or it reads as
  noise.

### Recommendation for the real build

- **Cadence: one `RequestScan` every ~10 s**, plus a manual `R` rescan. That is
  the honest floor given silent coalescing; anything faster is fiction.
- **Signals are the update source.** Subscribe to per-AP `PropertiesChanged`
  and to `AccessPointAdded`/`Removed`, and render on arrival. The scan timer
  exists only to prompt the driver, not to read results.
- Surface `LastScan` (delta) internally for "data is N seconds old" in the UI,
  and expose "last successful scan" rather than pretending every request
  succeeded.
- Do not build a fast-poll design on the assumption that NetworkManager will
  police it.

