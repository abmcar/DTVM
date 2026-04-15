#!/usr/bin/env bash
# G5: 双侧 correctness (spec §4.3 三项锚点:
#   223/223 JIT unit (multipass) + 215/215 interp unit + 2723/2723 Cancun state)
# ⚠️ correctness-{baseline,head}-20260415.log 已在 e3-ablation/ 存在,
#    默认不重跑; 本脚本仅在规则集变动时用。
set -euo pipefail

STAMP="$(date +%Y%m%d-%H%M%S)"
OUT_ROOT="docs/research/directions/peephole-optimization/submissions/experiments/e6-correctness-both-sides"

HEAD_LIB="$(pwd)/build/lib/libdtvmapi.so"
BASELINE_LIB="/home/abmcar/dtvm-baseline/build-baseline/lib/libdtvmapi.so"

for SIDE in with-peephole without-peephole; do
  case "${SIDE}" in
    with-peephole)    LIB="${HEAD_LIB}" ;;
    without-peephole) LIB="${BASELINE_LIB}" ;;
  esac
  OUT="${OUT_ROOT}/${SIDE}-${STAMP}"
  mkdir -p "${OUT}"

  echo "[G5] side=${SIDE} lib=${LIB}"

  # 1) JIT (multipass) unittests → 预期 223/223
  rc_jit=0
  EVMONE_EXTERNAL_OPTIONS="${LIB},mode=multipass" \
    /home/abmcar/evmone/build/bin/evmone-unittests \
    --gtest_filter="$(paste -sd: tests/evmone_unittests/EVMOneMultipassUnitTestsRunList.txt)" \
    > "${OUT}/unittests-multipass.log" 2>&1 || rc_jit=$?

  # 2) 解释器 unittests → 预期 215/215
  rc_interp=0
  EVMONE_EXTERNAL_OPTIONS="${LIB},mode=interpreter" \
    /home/abmcar/evmone/build/bin/evmone-unittests \
    --gtest_filter="$(paste -sd: tests/evmone_unittests/EVMOneInterpreterUnitTestsRunList.txt)" \
    > "${OUT}/unittests-interpreter.log" 2>&1 || rc_interp=$?

  # 3) Cancun state tests → 预期 2723/2723
  rc_state=0
  EVMONE_EXTERNAL_OPTIONS="${LIB},mode=multipass,enable_gas_metering=true" \
    /home/abmcar/evmone/build/bin/evmone-statetest \
    tests/fixtures/fixtures/state_tests \
    --vm external_vm -k fork_Cancun \
    > "${OUT}/statetest-cancun.log" 2>&1 || rc_state=$?

  # 汇总
  {
    echo "side=${SIDE} stamp=${STAMP}"
    echo "jit_unittests rc=${rc_jit}: $(grep -E "PASSED|FAILED" "${OUT}/unittests-multipass.log" | tail -1)"
    echo "interp_unittests rc=${rc_interp}: $(grep -E "PASSED|FAILED" "${OUT}/unittests-interpreter.log" | tail -1)"
    echo "statetest rc=${rc_state}: $(grep -E "PASSED|FAILED|passed|failed" "${OUT}/statetest-cancun.log" | tail -1)"
  } > "${OUT}/summary.txt"

  cat "${OUT}/summary.txt"
done

echo "[G5] done; logs in ${OUT_ROOT}/"
