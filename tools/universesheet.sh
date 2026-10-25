#!/bin/bash
# Renders every variant of a universe off screen (xvfb) into one contact sheet.
# usage: tools/universesheet.sh UNIVERSE OUT.png [LEVEL 0|1|2 (default 1)] [SIZE (default 640x360)] [SONG]
set -e
cd "$(dirname "$0")/.."
U=${1:?universe}; OUT=${2:?output png}; L=${3:-1}; SIZE=${4:-640x360}
SONG=${5:-}
TMP=$(mktemp -d)
mapfile -t VARS < <(xvfb-run -a ./build/zenscene --list 2>/dev/null | grep -i "^  $U[^:]*:" | sed 's/^  //')
[ ${#VARS[@]} -gt 0 ] || { echo "no variants for $U"; exit 1; }
i=0
for v in "${VARS[@]}"; do
    xvfb-run -a -s "-screen 0 1920x1080x24" ./build/zenscene --size "$SIZE" --review-shot "$TMP/$(printf %02d $i).png" \
        --level "$L" "$v" $SONG >/dev/null 2>&1 &
    i=$((i + 1))
done
wait
cols=4; [ $i -le 4 ] && cols=$i
montage "$TMP"/*.png -tile ${cols}x -geometry +2+2 "$OUT"
rm -rf "$TMP"
echo "$OUT: $i variants of $U at level $L"
