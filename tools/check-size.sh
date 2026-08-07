#!/usr/bin/env bash
LIMIT=1474560
TARGET=1400000
DIST="${1:-dist-win}"

TOTAL=$(find "$DIST" -type f -printf '%s\n' | awk '{s+=$1} END {print s+0}')
PCT=$(awk -v t="$TOTAL" -v l="$LIMIT" 'BEGIN{printf "%.1f", t*100/l}')

echo "-- size report --"
find "$DIST" -type f -printf '%10s  %p\n' | sort -rn
echo "total: $TOTAL / $LIMIT bytes (${PCT}%)  headroom: $((LIMIT-TOTAL))"

if   [ "$TOTAL" -gt "$LIMIT" ];  then echo "FAIL: over the hard limit"; exit 1
elif [ "$TOTAL" -gt "$TARGET" ]; then echo "WARN: over the safety target ($TARGET)"; exit 0
else echo "PASS"; fi
