#!/bin/sh
# Spike 002 — what can this phy + NetworkManager actually see?

set -eu

SCANS="${1:-5}"
INTERVAL="${2:-7}"

echo "=== NetworkManager / supplicant ==="
NetworkManager --version 2>&1 | head -1
wpa_supplicant -v 2>&1 | head -1
echo "wifi backend: $(grep -rhiE '^\s*wifi\.backend' /etc/NetworkManager/ 2>/dev/null || echo '(unset -> wpa_supplicant default)')"

echo
echo "=== wifi device ==="
DEV="$(nmcli -t -f DEVICE,TYPE device status | awk -F: '$2=="wifi"{print $1; exit}')"
echo "device: $DEV"
PHY="$(iw dev "$DEV" info 2>/dev/null | awk '/wiphy/{print "phy"$2}')"
echo "phy: $PHY"

echo
echo "=== regulatory ==="
iw reg get 2>&1 | head -12

echo
echo "=== bands the phy advertises (counted, with frequency span) ==="
# iw groups frequencies into Band 1 (2.4), Band 2 (5), Band 4 (6 GHz).
iw phy "$PHY" info 2>/dev/null | awk '
  /^[[:space:]]+Band [0-9]+:/ { band = $2; sub(":", "", band); low[band] = 0; high[band] = 0; next }
  /^[[:space:]]+\* [0-9.]+ MHz/ {
    if (band == "") next
    f = $2 + 0
    if (low[band] == 0 || f < low[band]) low[band] = f
    if (f > high[band]) high[band] = f
    count[band]++
  }
  END {
    for (b in count) printf "Band %s: %d channels, %d-%d MHz\n", b, count[b], low[b], high[b]
    if (count["4"] == 0) print "Band 4 (6 GHz): NOT ADVERTISED BY THE PHY"
  }'

echo
echo "=== the 6 GHz block in detail (first channels + their flags) ==="
iw phy "$PHY" info 2>/dev/null | awk '/^[[:space:]]+Band 4:/{f=1} f && /MHz \[/{print; n++} n>4{exit}'
echo "-- flag tally across all Band 4 channels --"
iw phy "$PHY" info 2>/dev/null | awk '/^[[:space:]]+Band 4:/{f=1} f && /MHz \[/{
  if ($0 ~ /\(no IR\)/) no_ir++
  if ($0 ~ /\(IR\)/) ir++
  if ($0 ~ /PSC/) psc++
  total++
} END { printf "%d channels: %d no-IR, %d IR, %d PSC\n", total, no_ir, ir, psc }'
echo "(no-IR channels can only be scanned passively — no beacon-refresh, so they appear only when an AP answers)"

echo
echo "=== $SCANS scans, $INTERVAL s apart: which bands actually answer? ==="
i=0
: > /tmp/spike002-freqs.txt
while [ "$i" -lt "$SCANS" ]; do
  i=$((i + 1))
  nmcli -f FREQ,CHAN,SIGNAL dev wifi list --rescan yes 2>/dev/null \
    | tail -n +2 | awk '{print $1}' >> /tmp/spike002-freqs.txt
  freq_count=$(sort -u /tmp/spike002-freqs.txt | grep -c . || true)
  echo "scan $i/$SCANS -> $freq_count distinct frequencies so far: $(sort -un /tmp/spike002-freqs.txt | tr '\n' ' ')"
  [ "$i" -lt "$SCANS" ] && sleep "$INTERVAL"
done

echo
echo "=== band summary ==="
sort -un /tmp/spike002-freqs.txt | awk '
  { f=$1+0
    if (f >= 5925) six = six " " f
    else if (f >= 5150) five = five " " f
    else if (f > 0) two = two " " f }
  END {
    printf "2.4 GHz: %s\n", (two == "" ? "(none seen)" : two)
    printf "5 GHz:   %s\n", (five == "" ? "(none seen)" : five)
    printf "6 GHz:   %s\n", (six == "" ? "(none seen)" : six)
  }'
rm -f /tmp/spike002-freqs.txt
