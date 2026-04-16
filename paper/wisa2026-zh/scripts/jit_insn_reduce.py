#!/usr/bin/env python3
"""
Count U256-indicator mnemonics in DTVM JIT assembly dumps.

Input: directory of `*.asm` files produced by `collect_jit_insn_dist.sh`.
Each file contains the full `dtvm --format evm --mode multipass` log; this
script isolates the `########## Assembly Dump ##########` section and
classifies each disassembled instruction line.

Heuristic classification (U256-indicator patterns, objdump syntax):
  * adc / adcq / adcx      — carry-chain add (strongest U256 signal)
  * sbb / sbbq / sbbx      — carry-chain subtract
  * movabs / movabsq        — 64-bit immediate (limb constants, masks)
  * shl/shr $0x40          — shift by 64 bits (limb-boundary shift)

The measurement is a pattern-based approximation of U256 multi-limb
arithmetic share, **not** a formal classification. A single instruction
line from objdump looks like:
    17:  49 8b 87 10 84 00 00   mov    0x8410(%r15),%rax
so we match mnemonics in the text-after-hex field.
"""

import glob
import os
import re
import sys


# Match objdump disasm line: "<addr>:\t<hex-bytes>\t<mnemonic> <ops>"
# Tolerant: accept variable whitespace. We split on tab/whitespace and take
# the first token after the hex-bytes column.
LINE_RE = re.compile(
    r"^\s*[0-9a-fA-F]+:\s+(?:[0-9a-fA-F]{2}\s+)+\s*([a-zA-Z][a-zA-Z0-9]*)\s*(.*)$"
)

U256_MNEMONICS = {
    "adc", "adcq", "adcx", "adcxq",
    "sbb", "sbbq", "sbbx", "sbbxq",
    "movabs", "movabsq",
}


def classify(mnemonic: str, operands: str) -> str:
    if mnemonic in U256_MNEMONICS:
        return "u256"
    # shl/shr by 64 bits (limb-boundary shift)
    if mnemonic in {"shl", "shr", "shlq", "shrq", "shll", "shrl"}:
        if re.search(r"\$0x40\b", operands):
            return "u256"
    return "other"


def scan(path: str):
    total = 0
    u256 = 0
    in_asm = False
    with open(path, errors="ignore") as f:
        for line in f:
            if "########## Assembly Dump" in line:
                in_asm = True
                continue
            if not in_asm:
                continue
            m = LINE_RE.match(line)
            if not m:
                continue
            mnemonic = m.group(1).lower()
            operands = m.group(2)
            c = classify(mnemonic, operands)
            total += 1
            if c == "u256":
                u256 += 1
    return total, u256


def main():
    if len(sys.argv) != 2:
        print("usage: jit_insn_reduce.py <dumps_dir>", file=sys.stderr)
        sys.exit(2)
    root = sys.argv[1]
    print("bench,total_insn,u256_insn,u256_share_pct")
    grand_t = 0
    grand_u = 0
    asm_files = sorted(glob.glob(os.path.join(root, "*.asm")))
    if not asm_files:
        print(f"warn: no *.asm in {root}", file=sys.stderr)
    for asm in asm_files:
        t, u = scan(asm)
        grand_t += t
        grand_u += u
        share = (u / t * 100) if t else 0.0
        bench = os.path.splitext(os.path.basename(asm))[0]
        print(f"{bench},{t},{u},{share:.2f}")
    share = (grand_u / grand_t * 100) if grand_t else 0.0
    print(
        f"# overall total={grand_t} u256={grand_u} share={share:.2f}%",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
