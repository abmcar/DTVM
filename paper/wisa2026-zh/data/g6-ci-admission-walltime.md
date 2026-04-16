# G6 — CI Admission Query Wall Time

Source: Local measurement via `verify_dmir_soundness.py` (Z3 4.15.4)
Date: 2026-04-16
Total rules queried: 70 dMIR (65 accepted + 5 seed)
Wall time (Config both, dual-layer): 0.50 seconds (median of 5 runs: 0.49, 0.50, 0.50, 0.53, 0.55)
Wall time (Config A only, production): 0.41 seconds
Method: `time python3 verify_dmir_soundness.py --rules dmir_rewrite_rules.json --config both --timeout 5000`
Machine: WSL2 Ubuntu 22.04, kernel 6.6.x

## Notes

- Only the 70 dMIR rules have Z3 equivalence queries (algebraic + execution layer).
- The 13 x86 CgIR rules are instruction-level patterns (erase/fold), verified by the test suite (evmone-unittests + evmone-statetest), not by Z3.
- "Config both" runs two passes per rule: Config A (with dread barrier, production) and Config B (without dread, hypothetical). Config A alone takes ~0.41s.
- No CI workflow for admission exists yet; the number comes from local timing. CI overhead (container startup, checkout) would add seconds, but the Z3 solve itself is <1s.
- All 70 rules pass: 63 sound + 7 vacuously safe (Config A); 65 sound + 5 unsound-CF (Config B, expected — dread-protected rules).
- Per-rule average: ~7 ms (both configs) / ~6 ms (Config A only).
- For the paper sentence "全量重新验证的墙钟时间约 X 秒": use **< 1 秒** or **约 0.5 秒** (dual-layer, 70 dMIR rules).
