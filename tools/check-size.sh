#!/usr/bin/env bash
set -uo pipefail

LIMIT=1474560
TARGET=1400000
DIST="${1:-dist-win}"

if [ ! -d "$DIST" ]; then
  echo "FAIL: '$DIST' does not exist - nothing was built"
  exit 1
fi

COUNT=$(find "$DIST" -type f | wc -l | tr -d ' ')
if [ "$COUNT" -eq 0 ]; then
  echo "FAIL: '$DIST' holds no files - refusing to report a pass"
  exit 1
fi

TOTAL=$(find "$DIST" -type f -print0 | xargs -0 cat | wc -c | tr -d ' ')
PCT=$(awk -v t="$TOTAL" -v l="$LIMIT" 'BEGIN{printf "%.1f", t*100/l}')

echo "-- size report --"
find "$DIST" -type f | while IFS= read -r f; do
  printf '%10s  %s\n' "$(wc -c < "$f" | tr -d ' ')" "$f"
done | sort -rn
echo "total: $TOTAL / $LIMIT bytes (${PCT}%)  headroom: $((LIMIT-TOTAL))  files: $COUNT"

if   [ "$TOTAL" -gt "$LIMIT" ];  then echo "FAIL: over the hard limit"; exit 1
elif [ "$TOTAL" -gt "$TARGET" ]; then echo "WARN: over the safety target ($TARGET)"; exit 0
else echo "PASS"; fi
