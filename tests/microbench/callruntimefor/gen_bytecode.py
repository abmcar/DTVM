#!/usr/bin/env python3
"""Generate microbench EVM bytecode for callRuntimeFor measurement.

Run: python3 tests/microbench/callruntimefor/gen_bytecode.py

Output: prints each fixture's body and full loop bytecode. Used to
regenerate the `code` field in the *.json fixtures after editing the
loop scaffolding or body templates.

Each bytecode is a tight loop iterating N=0xfde8 (65000) times. The body
either invokes the target opcode (forcing callRuntimeFor path) or POPs
the same stack delta to form a matched baseline.

Common scaffolding:

    PUSH1 0x00       ; counter (bottom of stack)
    JUMPDEST         ; loop top at offset 2
      <body>         ; performs +0 stack delta
      PUSH1 0x01
      ADD            ; counter += 1
      DUP1
      PUSH2 0xfde8
      GT             ; (65000 > counter) ? 1 : 0
      PUSH1 0x02
      JUMPI          ; loop back to JUMPDEST while counter < 65000
    POP              ; drop counter
    STOP
"""

# --- opcode constants ---
PUSH1, PUSH2, PUSH8, PUSH32 = "60", "61", "67", "7f"
JUMPDEST = "5b"
ADD, AND, NOT, OR = "01", "16", "19", "17"
DUP1, DUP2 = "80", "81"
SWAP1 = "90"
POP = "50"
GT, LT = "11", "10"
JUMPI = "57"
STOP = "00"
MULMOD, ADDMOD = "09", "08"
DIV, MOD = "04", "06"

# Loop bound: PUSH2 0xfde8 = 65000 iterations.
LOOP_BOUND_HEX = "fde8"


def emit(parts):
    s = "".join(parts)
    assert len(s) % 2 == 0, f"odd hex length: {s}"
    return s


def push2(value_hex_4chars):
    assert len(value_hex_4chars) == 4
    return PUSH2 + value_hex_4chars


def push8(value_hex_16chars):
    assert len(value_hex_16chars) == 16
    return PUSH8 + value_hex_16chars


def push32(value_hex_64chars):
    assert len(value_hex_64chars) == 64
    return PUSH32 + value_hex_64chars


def build_loop(body_hex):
    """Wrap a per-iteration body in a 65000-iteration loop with counter."""
    prelude = emit([PUSH1, "00"])  # PUSH1 0x00  (counter init)
    jumpdest_offset = len(prelude) // 2  # = 2
    jumpdest_byte = f"{jumpdest_offset:02x}"

    epilogue = emit([
        PUSH1, "01",
        ADD,
        DUP1,
        push2(LOOP_BOUND_HEX),
        GT,
        PUSH1, jumpdest_byte,
        JUMPI,
        POP,
        STOP,
    ])
    return emit([prelude, JUMPDEST, body_hex, epilogue])


# --- bodies (net stack delta 0) ---

# baseline_empty: 3 pushes (DUP1, PUSH8, PUSH8), 3 POPs.
baseline_empty_body = emit([
    DUP1,
    push8("0123456789abcdef"),
    push8("fedcba9876543210"),
    POP, POP, POP,
])

# mulmod_loop: DUP1 (non-const counter), PUSH8, PUSH8, MULMOD, POP.
# handleMulMod has no narrow path; non-const operand forces callRuntimeFor
# (evm_mir_compiler.cpp:2479).
mulmod_body = emit([
    DUP1,
    push8("0123456789abcdef"),
    push8("fedcba9876543210"),
    MULMOD,
    POP,
])

# addmod_loop: same shape with ADDMOD. u64 modulus has mod[3]==0,
# defeating handleAddMod's fast-path eligibility check
# (evm_mir_compiler.cpp:2304-2316). The dynamic SlowBB branch hits
# callRuntimeFor (line 2444-2455).
addmod_body = emit([
    DUP1,
    push8("0123456789abcdef"),
    push8("fedcba9876543210"),
    ADDMOD,
    POP,
])

# div_nonconst_loop: divisor with non-zero upper limbs (forcing the
# multi-limb branch in handleDivModGeneral, which calls callRuntimeFor
# at evm_mir_compiler.cpp:1847).
#   DUP1 (counter, to be divisor seed) +1
#   PUSH32 BIG_CONST                   +1
#   OR (combine: divisor = counter|BIG, non-const, upper-limbs set)  -1
#   DUP2 (bring counter as dividend on top)  +1
#   DIV                                -1
#   POP                                -1
# net 0.
BIG_CONST = "ffffffffffffffffffffffffffffffff00000000000000000000000000000001"
div_body = emit([
    DUP1,
    push32(BIG_CONST),
    OR,
    DUP2,
    DIV,
    POP,
])

# baseline_div: stack-shape mirror of div_body without DIV.
#   DUP1 +1, PUSH32 +1, DUP2 +1, POP×3 -3.  net 0.
baseline_div_body = emit([
    DUP1,
    push32(BIG_CONST),
    DUP2,
    POP, POP, POP,
])


FIXTURES = [
    ("baseline_empty",     baseline_empty_body),
    ("mulmod_loop",        mulmod_body),
    ("addmod_loop",        addmod_body),
    ("baseline_div",       baseline_div_body),
    ("div_nonconst_loop",  div_body),
]


if __name__ == "__main__":
    for name, body in FIXTURES:
        print(f"=== {name} ===")
        print(f"  body: 0x{body}")
        print(f"  code: 0x{build_loop(body)}")
        print()
