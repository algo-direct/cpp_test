#!/usr/bin/env bash
# Simple benchmark wrapper: runs mpmc_demo under perf stat when available
set -euo pipefail

BIN=./build/mpmc_demo
if [[ ! -x "$BIN" ]]; then
  echo "$BIN not found or not executable; build the project first (cmake/ninja)" >&2
  exit 2
fi

PROD=${1:-4}
CONS=${2:-4}
PER=${3:-1000000}
OUT=${4:-bench-results.txt}

PERF=perf
if ! command -v $PERF >/dev/null 2>&1; then
  echo "perf not found -- will run without perf stats" >&2
  echo "Running: $BIN --producers $PROD --consumers $CONS --per-producer $PER" >&2
  time $BIN --producers $PROD --consumers $CONS --per-producer $PER | tee "$OUT"
  exit 0
fi

echo "Running benchmark: producers=$PROD consumers=$CONS per-producer=$PER" | tee "$OUT"
echo "perf stat ..." | tee -a "$OUT"
$PERF stat -e cycles,instructions,cache-references,cache-misses,context-switches -r 3 $BIN --producers $PROD --consumers $CONS --per-producer $PER 2>&1 | tee -a "$OUT"

echo "Done. Results in $OUT"
