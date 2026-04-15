#!/usr/bin/env bash
# P3 协议：20 reps × 27 benches × 2 sides, stable-baseline + taskset 绑核
# WARNING: 本脚本 NEVER 覆盖 20reps-20260415.csv（spec 冻结 authoritative）
set -euo pipefail

FROZEN="docs/research/directions/peephole-optimization/submissions/experiments/e3-ablation/20reps-20260415.csv"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT_ROOT="docs/research/directions/peephole-optimization/submissions/experiments/e3-ablation/20reps-${STAMP}"

# 安全门：禁止生成到冻结路径
case "${OUT_ROOT}" in
  *20reps-20260415) echo "REFUSE: would overwrite frozen authoritative CSV"; exit 1 ;;
esac
mkdir -p "${OUT_ROOT}"

HEAD_LIB="$(pwd)/build/lib/libdtvmapi.so"
BASELINE_LIB="/home/abmcar/dtvm-baseline/build-baseline/lib/libdtvmapi.so"
EVMONE_BENCH="/home/abmcar/evmone/build/bin/evmone-bench"
BENCH_SUITE="/home/abmcar/evmone/test/evm-benchmarks/benchmarks"
REPS=20
CORES="${P3_CORES:-0-1}"   # taskset 绑核；默认用 core 0-1

for SIDE in with-peephole without-peephole; do
  case "${SIDE}" in
    with-peephole)    LIB="${HEAD_LIB}" ;;
    without-peephole) LIB="${BASELINE_LIB}" ;;
  esac
  OUT="${OUT_ROOT}/${SIDE}.json"
  echo "[P3] side=${SIDE} lib=${LIB} out=${OUT} cores=${CORES}"
  EVMONE_EXTERNAL_OPTIONS="${LIB},mode=multipass,enable_gas_metering=true" \
    taskset -c "${CORES}" \
    "${EVMONE_BENCH}" "${BENCH_SUITE}" \
      --benchmark_repetitions=${REPS} \
      --benchmark_report_aggregates_only=true \
      --benchmark_format=json \
      --benchmark_out="${OUT}"
done

python3 paper/wisa2026-zh/scripts/p3_reduce.py \
  "${OUT_ROOT}/with-peephole.json" \
  "${OUT_ROOT}/without-peephole.json" \
  > "${OUT_ROOT}/summary.csv"

echo "[P3] done -> ${OUT_ROOT}/"
echo "[P3] frozen authoritative remains: ${FROZEN}"
