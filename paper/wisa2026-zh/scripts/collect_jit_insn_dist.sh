#!/usr/bin/env bash
# Collect DTVM multipass JIT assembly dumps for all 27 evmone-bench cases
# and reduce to per-bench U256-share + overall number (G1 — §2.1 (c)).
#
# The JIT-logging binary is built separately with
#   cmake -DZEN_ENABLE_JIT_LOGGING=ON -DCMAKE_BUILD_TYPE=Release ...
# into build-jitlog/. The binary emits MIR/CgIR/Assembly dumps to stderr
# (and objdump output to stdout). We merge with `2>&1`.
#
# Each benchmark JSON under /home/abmcar/evmone/test/evm-benchmarks/benchmarks
# may declare multiple cases via `transaction.data[]`; we emit one .asm per
# contract (deploy code compiled once), since JIT output is keyed on the
# contract code, not the calldata.

set -euo pipefail

REPO_ROOT="${REPO_ROOT:-$(pwd)}"
DTVM_BIN="${DTVM_BIN:-${REPO_ROOT}/build-jitlog/dtvm}"
BENCH_ROOT="${BENCH_ROOT:-/home/abmcar/evmone/test/evm-benchmarks/benchmarks}"
OUT_ROOT="${OUT_ROOT:-${REPO_ROOT}/docs/research/directions/peephole-optimization/submissions/experiments/e7-jit-insn-distribution}"
REDUCE_PY="${REDUCE_PY:-${REPO_ROOT}/paper/wisa2026-zh/scripts/jit_insn_reduce.py}"

if [[ ! -x "${DTVM_BIN}" ]]; then
  echo "error: DTVM binary not found at ${DTVM_BIN}" >&2
  echo "build with: cmake -B build-jitlog -DCMAKE_BUILD_TYPE=Release -DZEN_ENABLE_JIT_LOGGING=ON -DZEN_ENABLE_EVM=ON -DZEN_ENABLE_LIBEVM=ON -DZEN_ENABLE_MULTIPASS_JIT=ON -DZEN_ENABLE_SINGLEPASS_JIT=OFF -DZEN_ENABLE_CPU_EXCEPTION=ON -DZEN_ENABLE_SPEC_TEST=ON && cmake --build build-jitlog --target dtvm -j" >&2
  exit 2
fi

mkdir -p "${OUT_ROOT}/dumps" "${OUT_ROOT}/hex"

# Extract (bench_label, addr, hex_code) tuples from every JSON under
# main/ and micro/. Each JSON case's `pre.<addr>.code` is a compiled
# contract; many JSONs have exactly one contract + calldata cases. Label
# is <subdir>_<bench_name>.
python3 - "${BENCH_ROOT}" "${OUT_ROOT}/hex" <<'PY'
import glob, json, os, sys
bench_root, out_hex = sys.argv[1], sys.argv[2]
os.makedirs(out_hex, exist_ok=True)
for jf in sorted(glob.glob(os.path.join(bench_root, "*", "*.json"))):
    sub = os.path.basename(os.path.dirname(jf))
    name = os.path.splitext(os.path.basename(jf))[0]
    with open(jf) as f:
        d = json.load(f)
    for tname, td in d.items():
        pre = td.get("pre", {})
        # benchmark contracts live at non-sender addresses; pick the one
        # with the longest code (the actual contract under test).
        best = ("", "")
        for addr, state in pre.items():
            code = state.get("code", "")
            if isinstance(code, str) and len(code) > len(best[1]):
                best = (addr, code)
        if best[1]:
            label = f"{sub}__{name}"
            hex_path = os.path.join(out_hex, f"{label}.hex")
            # dtvm accepts hex with or without 0x prefix; the EVM loader
            # strips 0x. Keep 0x for readability.
            with open(hex_path, "w") as fp:
                fp.write(best[1].strip())
            print(label)
PY

# Run dtvm on each extracted hex and capture the full dump.
for hex_path in "${OUT_ROOT}"/hex/*.hex; do
  label="$(basename "${hex_path}" .hex)"
  out_asm="${OUT_ROOT}/dumps/${label}.asm"
  echo "[G1] ${label}" >&2
  # --calldata 0x ensures the JIT compiles the dispatch path; even if the
  # benchmark normally calls a specific function, we are only measuring
  # what dMIR lowering emits per-basic-block. Use no-op calldata to avoid
  # revert-loops that shortcut compilation.
  if ! "${DTVM_BIN}" --format evm --mode multipass \
        --calldata 0x "${hex_path}" > "${out_asm}" 2>&1; then
    # Non-zero exit is expected (benchmark contracts may revert on empty
    # calldata or run out of gas). JIT compile still happens and dump is
    # still written; keep the file.
    echo "  note: dtvm non-zero exit (JIT dump still captured)" >&2
  fi
done

python3 "${REDUCE_PY}" "${OUT_ROOT}/dumps" > "${OUT_ROOT}/summary.csv" 2> "${OUT_ROOT}/summary.stderr"

echo "[G1] dumps: $(ls "${OUT_ROOT}/dumps" | wc -l)"
echo "[G1] summary: ${OUT_ROOT}/summary.csv"
cat "${OUT_ROOT}/summary.stderr"
