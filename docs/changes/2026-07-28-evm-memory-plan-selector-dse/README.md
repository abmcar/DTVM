# Change: Reject incomplete EVM memory facts

- **Status**: Verified
- **Date**: 2026-07-28
- **Tier**: Light

## Overview

Multipass now publishes opcode-level memory facts only when the analyzer has a
reliable block-entry stack. Blocks with incomplete facts retain their control
flow graph topology and act as conservative barriers for every memory-plan
consumer. This prevents dead-store elimination from removing a selector store
that a subsequent `KECCAK256` observes.

## Motivation

Mainnet block 21800000 first diverges at transaction 15 in the 0x Exchange
proxy. The interpreter hashes the selector `0x803ba26d` with the proxy mapping
slot and reads the implementation from storage. Multipass instead hashes a
zero selector, reads an empty slot, and reverts. Commit `da0678e` introduced
the regression; its parent and the same commit with the memory-plan framework
disabled both execute the block correctly.

## Impact

The change affects only the facts supplied to multipass EVM memory-plan
consumers. Unreliable blocks lose optimization proofs but retain their CFG
identity. The EVM gas schedule, EVMC host contract, Reth witness, and replay
expectations are unchanged.

The conservative fallback can reduce memory-plan optimization in blocks whose
entry stack comes from dynamic dispatch. Blocks with a reliable static entry
stack retain the existing facts, prechecks, grouping, load forwarding, and
dead-store proofs.

## Root cause

The first bad commit, `da0678e`, passed an unresolved analyzer entry depth of
`-1` to the memory-facts builder as depth zero. The failing proxy block entered
through a dynamic jump with hidden stack values. Simulating `SWAP3` from an
empty abstract stack silently made it a no-op, so the builder recorded both
stores at offset zero and recorded the following `KECCAK256` read at offset 64
with unknown size. Dead-store elimination consumed those false facts and
removed the selector store even though `KECCAK256` observes it.

## Design

Blocks with unresolved, inconsistent, or dynamic-dispatch-tainted entry stack
depth retain their CFG topology but do not publish opcode-level memory facts.
The taint matters even when the analyzer has a non-negative resolved depth:
indirect dispatch can still supply a hidden runtime prefix that the static
depth under-counts.

The implementation enforces the fallback at four seams:

- The bytecode visitor accepts an entry stack only when its depth is resolved,
  consistent, and not derived from dynamic dispatch.
- An explicit `HasCompleteOpcodeFacts` marker distinguishes an unreliable block
  from a reliable block that contains no memory operations.
- `MemoryFactsBuilder::observeOpcode` is a no-op while an incomplete block is
  current. It publishes no operation and does not mutate the abstract stack or
  value numbering, even if a future caller omits the visitor-side fast path.
- Guaranteed-minimum analysis resets incomplete blocks to zero. Linear-region
  grouping treats them as unknown-effect barriers. Precheck, grouping,
  dead-store, and load-forwarding consumers therefore cannot derive a proof
  from partial facts or across the missing dynamic edge.

## Verification

### Regression tests

The baseline-red variants establish each layer of the contract:

- The unresolved-entry test fails on the first-bad baseline because it
  publishes false memory operations at PCs 65, 71, and 76.
- The two production visitor tests fail on the baseline because dynamic
  targets publish memory operations at PCs 7 and 28.
- The two consumer tests fail when the incomplete marker is present without
  conservative consumer handling. The invalid guarantees are 160 and 64 bytes.
- The direct builder test fails when `observeOpcode` is permitted to process an
  incomplete current block: the block publishes two operations and pollutes the
  following complete block.

The final implementation passes 7/7 focused regressions. They cover the exact
selector-store/two-word-hash motif, unresolved and dynamic-dispatch-tainted
visitor entries, the builder-owned no-op invariant, cross-block
guaranteed-minimum analysis, and the planner's precheck, grouping, dead-store,
and load-forwarding results.

### Build and suites

The final library has SHA-256
`1f71e0d477846785281b1afca6d0ebf52a55898a12dcfc3c14ee2019bcfea787`.
The captured incremental build exits zero and contains no compiler warning
lines. The same source and build pass:

- 115/115 EVM frontend tests;
- 65/65 interpreter-versus-multipass differential tests;
- 223/223 external-VM unit tests in `dtvm_local_test.sh --auto`;
- 209/209 multipass EVM assembly tests; and
- 12/12 CTest targets.

The external state-test suite passes 2710/2723 cases and fails 13 cases, so
`dtvm_local_test.sh --auto` exits one. The final library reproduces the same 13
failing test names seen during hardening. A same-environment filtered
comparison captured during that hardening reproduced the same failing set and
state-root mismatch lines on clean `upstream/main` and the candidate. The
evmone binary and EEST fixtures are external `DTVM-LabPerf` assets rather than
repository-owned test assets; this result is recorded as an
asset-compatibility baseline, not a regression caused by this change.

### Block 21800000

The final builder-hardened library replays the original failing block in
explicit `multipass` mode with exit code zero. The run executes 58
transactions and validates gas used `22416172`, the receipt root, logs bloom,
and proof-union post-state. The sealed identities are:

- runner SHA-256:
  `26a902e9c6fa77121e5b3f7851afdead2fd85d5f8f03630c553e8eec4a4dcf1f`;
- one-block corpus SHA-256:
  `03465cf38697bbb5fdd1f327685fa78048d8d37aba80ec80d3c8b8c9bffee93a`;
- result:
  `/home/wyh/Dev/reth-dtvm-20260724T020354Z/evidence/kwrongblockgas-20260728/fix-validation/04-prefix001-final-builder-hardened-attempt-002/result.json`.

The sealed four-block replay uses the same library SHA-256
`1f71e0d477846785281b1afca6d0ebf52a55898a12dcfc3c14ee2019bcfea787`
and exits zero. Blocks 21800000 through 21800003 execute 499 transactions with
total gas used `70826847`. The run records `fresh_db`,
`db_state_access_audited`, `header_body_prevalidated`,
`gas_receipt_root_bloom_checked`, `per_block_post_compared`, and
`union_post_compared` as true.

The four-block result has SHA-256
`516ad8f95e412fd4c2b86d5a52f9c849df4cdc7800b6baef068503741973f475`;
the exported post-state has SHA-256
`d3280d389c11c381027f53df44961d1cdaddc6080db52b661306f9e02c3bfe21`.
Both artifacts are stored under
`/home/wyh/Dev/reth-dtvm-20260724T020354Z/evidence/kwrongblockgas-20260728/fix-validation/05-prefix004-final-builder-hardened/`.

## Quality-gate limitations

The final `tools/format.sh check`, captured at 2026-07-28 11:14:20 UTC and
bound to library SHA-256
`1f71e0d477846785281b1afca6d0ebf52a55898a12dcfc3c14ee2019bcfea787`,
exits zero with empty standard output. The changed C++ files also pass their
direct clang-format check, `git diff --check` passes, and `tests/evm_asm` has
no diff from `upstream/main`.

Test and formatting tools generated ignored outputs and changed six tracked
EVM assembly fixtures during intermediate runs. The ignored outputs were
removed and the tracked fixtures were restored exactly to `upstream/main`;
none of that generated noise remains in the change.

`tools/check_change_doc.py` is absent from this repository checkout. Both
attempts to run the required declaration check therefore exit 2 with a
file-not-found error; this document does not claim that gate passed. The
missing helper does not invalidate the recorded code, test, or replay evidence,
but the declaration gate remains incomplete.

## Checklist

- [x] Implementation complete
- [x] Tests added/updated
- [x] Module spec reviewed; the documented conservative fallback contract is
      unchanged
- [x] Final library build and in-repository focused, frontend, differential,
      EVM assembly, and CTest suites pass
- [x] Original block 21800000 passes with the final builder-hardened library
- [x] Sealed four-block replay passes with the same library SHA
- [ ] Change-document declaration helper becomes available and passes
