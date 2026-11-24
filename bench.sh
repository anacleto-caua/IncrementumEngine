#!/usr/bin/env bash
# Unattended benchmark suite for the TCC test scenarios (test_scenerario.md). Linux port of bench.cmd;
# keep the two in step.
#
#   ./bench.sh          full suite: 5 repetitions per configuration (~2.5-3 h)
#   ./bench.sh quick    1 repetition, short durations - checks the pipeline end to end (~8 min)
#
#   BENCH_DRY_RUN=1 ./bench.sh [full|quick]
#                       prints the build and run commands without building, running or writing
#                       anything.
#
# Builds release, then appends every run to ONE file:
#   results/bench_<hostname>_<yyyyMMdd_HHmm>.jsonl
# Line 1 describes the machine; every following line is one run (or a skipped/failed marker).
# Load with: pandas.read_json(path, lines=True)
#
# Works when started over SSH: the graphical session's Wayland/X variables are filled in if unset.

set -u

cd "$(dirname "$(readlink -f "$0")")" || exit 1
REPO_DIR="$(pwd)"

PROFILE="${1:-full}"
DRY_RUN="${BENCH_DRY_RUN:-0}"

case "${PROFILE,,}" in
    full)
        REPS=5
        DURATION=()
        WARMUP=()
        SOAK_DURATION=()
        C1_WORKERS=(1 2 4 half max-1)
        ;;
    quick)
        REPS=1
        DURATION=(--duration=10)
        WARMUP=(--warmup=3)
        SOAK_DURATION=(--duration=60)
        C1_WORKERS=(1 half)
        ;;
    *)
        echo "usage: bench.sh [full|quick]"
        exit 1
        ;;
esac

# Over SSH none of the session's display variables are set; point at the local user session.
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
export DISPLAY="${DISPLAY:-:0}"

build_failed() {
    echo "Build failed - not running the suite."
    exit 1
}

echo "=== Building release ==="
if [ "$DRY_RUN" = "1" ]; then
    echo "[dry run] xmake f -m release -p linux -y"
    echo "[dry run] xmake"
else
    xmake f -m release -p linux -y || build_failed
    xmake || build_failed
fi

STAMP="$(date +%Y%m%d_%H%M)"
# uname -n is the same nodename the engine writes as "hostname" in the sysinfo line.
OUT="$REPO_DIR/results/bench_$(uname -n)_$STAMP.jsonl"
if [ "$DRY_RUN" != "1" ]; then
    mkdir -p "$REPO_DIR/results"
fi

# The engine loads shaders/assets relative to the working directory.
BIN_DIR="$REPO_DIR/build/linux/release"
EXE="$BIN_DIR/IncrementumEngine"
if [ "$DRY_RUN" != "1" ] && [ ! -x "$EXE" ]; then
    echo "Built binary not found at $EXE"
    exit 1
fi
cd "$BIN_DIR" 2>/dev/null || [ "$DRY_RUN" = "1" ] || exit 1

run() {
    echo "[$(date +%H:%M:%S.%2N)] IncrementumEngine $*"
    if [ "$DRY_RUN" = "1" ]; then
        echo "    [dry run] $EXE --out=$OUT $*"
        return 0
    fi
    "$EXE" "--out=$OUT" "$@"
    # A crash shows up as 128 + the signal number (e.g. 139 for SIGSEGV).
    local exit_code=$?
    if [ "$exit_code" != "0" ]; then
        echo "    FAILED with exit code $exit_code"
        echo "{\"type\":\"run_failed\",\"args\":\"$*\",\"exit\":$exit_code}" >> "$OUT"
    fi
    return 0
}

repetition() {
    local r=$1
    echo
    echo "=== Repetition $r of $REPS ==="
    for w in "${C1_WORKERS[@]}"; do run --bench=c1_coldstart --workers="$w" --run="$r"; done
    for s in c2_static c2_slow c2_fast c2_spin c2_spin_move; do
        run --bench="$s" --run="$r" "${DURATION[@]}" "${WARMUP[@]}"
    done
    run --bench=c3 --run="$r" --width=1280 --height=720 "${DURATION[@]}" "${WARMUP[@]}"
    run --bench=c3 --run="$r" --width=1920 --height=1080 "${DURATION[@]}" "${WARMUP[@]}"
    # One soak per suite is enough - it's a stability check, and it's 15 minutes.
    if [ "$r" = "1" ]; then run --bench=c4_soak --run="$r" "${SOAK_DURATION[@]}" "${WARMUP[@]}"; fi
}

echo "=== Machine info ==="
run --bench=sysinfo

# Repetitions are interleaved (every configuration once, then again) rather than run back to
# back, so slow drift - thermals, background activity - spreads across configurations instead
# of landing on whichever ran last.
for ((r = 1; r <= REPS; r++)); do repetition "$r"; done

echo
echo "=== Done ==="
echo "Results: $OUT"
exit 0
