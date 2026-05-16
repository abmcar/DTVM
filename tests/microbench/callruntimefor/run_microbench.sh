#!/usr/bin/env bash
# Microbench driver for callRuntimeFor cycle/op measurement.
#
# Usage: bash tests/microbench/callruntimefor/run_microbench.sh
# Env overrides:
#   DTVMAPI       path to libdtvmapi.so (default: ../../../build/lib/libdtvmapi.so
#                                       fallback: /home/abmcar/DTVM/build/lib/libdtvmapi.so)
#   EVMONE_BENCH  path to evmone-bench binary (default: /home/abmcar/evmone/build/bin/evmone-bench)
#   REPS          benchmark repetitions (default: 5)
#
# Output: result.json (raw Google Benchmark JSON) and a printed cycle/op summary.

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKTREE_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# Locate libdtvmapi.so: prefer worktree-local build, fallback to main DTVM build.
if [[ -z "${DTVMAPI:-}" ]]; then
  if [[ -f "$WORKTREE_ROOT/build/lib/libdtvmapi.so" ]]; then
    DTVMAPI="$WORKTREE_ROOT/build/lib/libdtvmapi.so"
  elif [[ -f "/home/abmcar/DTVM/build/lib/libdtvmapi.so" ]]; then
    DTVMAPI="/home/abmcar/DTVM/build/lib/libdtvmapi.so"
  else
    echo "error: cannot locate libdtvmapi.so" >&2
    echo "  tried: $WORKTREE_ROOT/build/lib/libdtvmapi.so" >&2
    echo "         /home/abmcar/DTVM/build/lib/libdtvmapi.so" >&2
    echo "  set DTVMAPI=<path> to override" >&2
    exit 1
  fi
fi

EVMONE_BENCH="${EVMONE_BENCH:-/home/abmcar/evmone/build/bin/evmone-bench}"
REPS="${REPS:-5}"

if [[ ! -f "$DTVMAPI" ]]; then
  echo "error: DTVMAPI not found: $DTVMAPI" >&2
  exit 1
fi
if [[ ! -x "$EVMONE_BENCH" ]]; then
  echo "error: EVMONE_BENCH not executable: $EVMONE_BENCH" >&2
  exit 1
fi

VM_ARG="${DTVMAPI},mode=multipass,enable_gas_metering=true"

echo "==> running evmone-bench with DTVM"
echo "    DTVMAPI=$DTVMAPI"
echo "    EVMONE_BENCH=$EVMONE_BENCH"
echo "    REPS=$REPS"
echo

RESULT_JSON="$SCRIPT_DIR/result.json"

# --benchmark_min_time=1x avoids re-JIT between repetitions (per
# reference_evmone_bench_iteration_semantics: Ns re-JITs each iter).
# --benchmark_filter restricts to our fixtures (excludes synth benches).
"$EVMONE_BENCH" "$VM_ARG" "$SCRIPT_DIR/fixtures" \
  --benchmark_min_time=1x \
  --benchmark_repetitions="$REPS" \
  --benchmark_filter='external/total/callruntime/' \
  --benchmark_format=json > "$RESULT_JSON" 2>&1 || {
  echo "error: evmone-bench failed; tail of output:" >&2
  tail -n 30 "$RESULT_JSON" >&2
  exit 1
}

# Strip the "External VM: ..." header line so the file is pure JSON.
# (evmone-bench writes that line to stdout before the JSON body.)
python3 - "$RESULT_JSON" <<'PY'
import json, sys, pathlib
p = pathlib.Path(sys.argv[1])
raw = p.read_text()
i = raw.find('{')
if i > 0:
    p.write_text(raw[i:])
PY

echo "Result saved to: $RESULT_JSON"
echo

# Compute CPU frequency in GHz for cycle conversion. lscpu gives a
# representative "CPU MHz" or "BogoMIPS" — for the WSL2 host, use
# the maximum from /proc/cpuinfo's "cpu MHz" entries (avoids the
# scaled-down idle frequency).
CPU_MHZ="$(awk -F: '/cpu MHz/ {gsub(/ /,"",$2); print $2}' /proc/cpuinfo | sort -rn | head -1)"
if [[ -z "$CPU_MHZ" ]]; then
  # Fallback to benchmark.json's reported mhz_per_cpu
  CPU_MHZ="$(python3 -c "import json; d=json.load(open('$RESULT_JSON')); print(d['context']['mhz_per_cpu'])")"
fi

# Loop iteration count inside the EVM bytecode: PUSH2 0xfde8 = 65000.
ITER_N=65000

python3 - "$RESULT_JSON" "$CPU_MHZ" "$ITER_N" <<'PY'
import json, sys
result_path, cpu_mhz_s, iter_n_s = sys.argv[1], sys.argv[2], sys.argv[3]
cpu_ghz = float(cpu_mhz_s) / 1000.0
iter_n = int(iter_n_s)

with open(result_path) as f:
    data = json.load(f)

# Extract MEAN cpu_time for each fixture
means = {}
stddevs = {}
for b in data['benchmarks']:
    name = b.get('aggregate_name')
    full = b['name']
    if name == 'mean':
        # Strip "_mean" suffix; key by fixture-stem
        # e.g. external/total/callruntime/mulmod_loop/loop_mean
        parts = full.split('/')
        stem = parts[3]
        means[stem] = b['cpu_time']
    elif name == 'stddev':
        parts = full.split('/')
        stem = parts[3]
        stddevs[stem] = b['cpu_time']

if not means:
    print("ERROR: no aggregate (mean) results — did REPS<2?")
    sys.exit(1)

# Convert us → ns/iteration (each run is `iter_n` iterations of the bytecode loop)
def ns_per_iter(us_per_run):
    return us_per_run * 1000.0 / iter_n

def cycles_per_iter(us_per_run):
    return ns_per_iter(us_per_run) * cpu_ghz

print(f"CPU GHz: {cpu_ghz:.3f}")
print(f"Iterations per bytecode run: {iter_n}")
print()
print(f"{'Fixture':<25} {'mean_ns/run':>12} {'stddev_ns/run':>14} {'ns/iter':>10} {'cycles/iter':>13}")
print("-" * 80)
for stem in ['baseline_empty', 'mulmod_loop', 'addmod_loop', 'baseline_div', 'div_nonconst_loop']:
    if stem not in means:
        continue
    m = means[stem]
    s = stddevs.get(stem, 0.0)
    # cpu_time is in us; convert to ns
    m_ns_run = m * 1000.0
    s_ns_run = s * 1000.0
    ni = ns_per_iter(m)
    ci = cycles_per_iter(m)
    print(f"{stem:<25} {m_ns_run:>12.0f} {s_ns_run:>14.0f} {ni:>10.2f} {ci:>13.1f}")

print()
print("=== callRuntimeFor cost (test - matched baseline) ===")
def delta_cycles(test_stem, base_stem):
    if test_stem not in means or base_stem not in means:
        return None, None
    delta_us = means[test_stem] - means[base_stem]
    delta_ns_iter = delta_us * 1000.0 / iter_n
    delta_cycles = delta_ns_iter * cpu_ghz
    return delta_ns_iter, delta_cycles

# Map test → matched baseline
pairs = [
    ('mulmod_loop', 'baseline_empty', 'MULMOD callRuntimeFor (unconditional, line 2479)'),
    ('addmod_loop', 'baseline_empty', 'ADDMOD callRuntimeFor (slow path, mod[3]==0)'),
    ('div_nonconst_loop', 'baseline_div', 'DIV callRuntimeFor (multi-limb branch, line 1842)'),
]
print(f"{'Op':<45} {'delta ns/op':>12} {'cycles/op':>11}  bucket")
print("-" * 90)
for test, base, label in pairs:
    dn, dc = delta_cycles(test, base)
    if dn is None:
        print(f"{label:<45}    MISSING (test={test}, base={base})")
        continue
    if dc < 20:
        bucket = "<20"
    elif dc < 100:
        bucket = "20-100 gray"
    else:
        bucket = ">100"
    print(f"{label:<45} {dn:>12.2f} {dc:>11.1f}  {bucket}")
PY
