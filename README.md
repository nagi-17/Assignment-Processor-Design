<h1 align="center">RISC201</h1>

**RISC201** is a 32-bit RISC instruction set architecture and a pair of pipelined processors that implement it, built from scratch in C++.

It ships two pipelines, a vertically microprogrammed control unit, an IEEE-754 floating-point unit, and a complete software suite — assembler, disassembler, CLI simulator and debugger.



## Pipelines

| Pipeline | Stages |
| :--- | :--- |
| **4-stage** | IF, ID, EX, merged MEM/WB |
| **6-stage** | IF, ID, EX, MA1 (address + stack-boundary check), MA2 (data access + global hazard unit), WB |

Both start from the classic five stages. The 4-stage merges MEM and WB because the memory stage is idle for most instructions and the merge costs no forwarding path. The 6-stage splits MEM instead of EX, because splitting EX would put ALU-to-ALU dependencies — the most frequent kind — on the critical path.

## Architecture

- **Control unit** — vertical microprogrammed. The opcode addresses a control ROM whose word is expanded into the control signals carried through the pipeline, so new instructions are new ROM entries rather than new gates.
- **ALU** — Kogge-Stone adder (`add`, `sub`, `cmp`, addressing), barrel shifter (all shifts in one pass), Radix-4 Booth + Wallace-tree multiplier, Radix-4 divider.
- **FPU** — IEEE-754 binary32 `fadd`, `fsub`, `fmul`, `fdiv`, `fcmp`, with denormals flushed to zero, round-to-nearest-even, and a Newton-Raphson divider that reuses the multiplier.
- **Hazards** — interlock-plus-forwarding: a Data Lock Unit stalls load-use pairs, forwarding covers the rest, and a Branch Lock Unit squashes speculatively fetched instructions when a branch is resolved in EX.
- **Traps** — `ecall`, `chk`, `eret`, `ebreak` and `wfi` are resolved in the global hazard unit, with `epc` and `cause` recording the trap and the fetch multiplexer prioritising trap > `eret` > branch > stall.

## Instruction Set

All instructions are 32 bits, with a 6-bit opcode whose top two bits select the class.

| Class | Instructions |
| :--- | :--- |
| `00` Integer ALU | `nop`, `add`, `sub`, `mul`, `div`, `mod`, `cmp`, `and`, `or`, `xor`, `not`, `lsl`, `lsr`, `asr` |
| `01` Floating point | `fadd`, `fsub`, `fmul`, `fdiv`, `fcmp`, `fmov` |
| `10` Memory & system | `ld`, `st`, `movu`, `movh`, `csrr`, `csrw`, `chk`, `clrz` |
| `11` Control flow | `b`, `beq`, `bne`, `bgt`, `blt`, `call`, `ret`, `ecall`, `eret`, `ebreak`, `wfi` |

The register file is a unified set of 16 32-bit registers (`r8`–`r11` alias `f0`–`f3`), with `r12` hardwired to zero, `r13`–`r15` as frame pointer, stack pointer and return address. `pc`, `flags.E`, `flags.GT`, `epc` and `cause` are system registers, reachable only through `csrr`/`csrw`.

`push` and `pop` are assembler macros; `mov` is an `add` against the zero register.

## Software

- **Assembler** — two-pass; records labels and expands `push`/`pop`, then encodes to 32-bit binary strings.
- **Disassembler** — rebuilds assembly text from binary or hex words, used to cross-check the assembler.
- **Simulator** — interactive CLI running either pipeline, with register, pipeline-latch and microPC inspection.
- **Debugger** — step execution, breakpoints and state inspection, entered via `ebreak` or CLI commands.

## Building and Running

```bash
make                              # build ./simulator and ./assembler
./simulator tests/test_basic.s    # run a program on both pipelines and compare
./simulator                       # interactive CLI: load, run4, run6, debug4, debug6, compare
make test                         # run the unit-test suites
```

Requires a C++17 compiler. Windows equivalents of the build recipes are in the `*.bat` scripts.

## Report

The full design rationale, hazard analysis, test results and cycle-level measurements are in
[`COA Project Report.pdf`](COA%20Project%20Report.pdf).

## Team

Dhyaan Shah(25114032) · Hitansh Gandhi(25114044) · Nitesh Anand(25114065) · Ritik Yadav(25114079) · Vansh Gupta(25114097)
