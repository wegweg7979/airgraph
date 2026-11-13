# Spike 002 — does this machine actually surface 6 GHz access points?

## Question

Given an AX210 (Wi-Fi 6E) and NetworkManager 1.58 on Fedora 45, when a scan
runs, then 6 GHz BSSIDs (frequency ≥ 5925 MHz) appear in the results — or we
know exactly why they cannot, so the app can say so instead of drawing an
empty 6 GHz band.

## Approach

Evidence-gathering, no code: the capability lives in the firmware, the
regulatory database, and NetworkManager's scan logic — not in anything we can
write. `probe.sh` prints, in one shot:

1. the six-GHz bands the phy advertises (`iw phy` / `iw reg get`)
2. NetworkManager version and Wi-Fi backend (wpa_supplicant vs iwd)
3. several consecutive NM scans, collapsing every observed frequency into
   the set of bands actually reported

## Run

```
spikes/002-six-ghz/probe.sh 5 7     # 5 scans, 7 s apart
```

## Results

See `results.txt`.

## Verdict: PARTIAL

The phy and the regulatory domain are both 6 GHz-ready; the *environment* has
no 6 GHz access point to prove end-to-end reception with.

### What worked

- **The phy advertises the band properly:** `iw phy phy0` reports Band 1
  (14 channels, 2412–2484 MHz), Band 2 (37 channels, 5180–5905 MHz) and
  **Band 4 (59 channels, 5955–7115 MHz)** on the AX210.
- **The regulatory domain allows it:** global country `GB` grants
  `5925 - 6425 @ 320`, and 6 GHz channels come back at 22 dBm — client-usable.
- The scan path itself is current: NetworkManager **1.58.1** with
  wpa_supplicant **2.11**, both well past any 6 GHz support threshold.
- Five scans, 7 s apart, produced a stable and sane picture of the air:
  2.4 GHz on 2412/2462, 5 GHz on 5180/5500/5620/5765.

### What didn't

- **Zero 6 GHz access points seen** in five scans — the honest reading is that
  none are in range, not that scanning is broken. Nothing in this environment
  can distinguish "no 6E AP nearby" from "6 GHz scan silently skipped", so the
  app must not present an empty 6 GHz band as an error either way.
- 24 of the 59 Band 4 channels are flagged `(no IR)`: passive-scan-only, so
  they produce nothing unless an AP answers a probe there.
- Channel width is still absent everywhere in this path.

### Surprises

- The 5 GHz span the phy claims (5180–5905) is wider than what GB actually
  grants the radio, and the observed 5 GHz BSSIDs moved between runs — 5620 one
  run, 5765 the next. Scan results are a sample of the air, not an inventory.
- The phy is `self-managed` (`country DE: DFS-UNSET`), i.e. wpa_supplicant owns
  the regulatory state, not the kernel — worth knowing before ever suggesting
  an `iw reg set`.

### Recommendation for the real build

- Keep the 6 GHz band as a **first-class view that can be empty**, with an
  explicit "no 6 GHz APs heard" state and the phy's capability shown
  separately — capability and observation are two different facts.
- Read the band plan from `iw phy` (channel count and span per band) so the
  graph's x-axis reflects this radio, not a hardcoded table.
- Do not promise 6 GHz coverage. Report what was heard, and flag `no IR`
  channels as passive-only if the user asks why nothing appears there.

