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

### Three ways to run a program

| Mode | How to start | Behaviour |
| :--- | :--- | :--- |
| **Batch** | `./simulator prog.s` | Runs to completion, prints the pipeline comparison and statistics. |
| **CLI debugger** | `./simulator --debug6 prog.s` | Line-oriented `debug>` prompt (`step`, `run`, `break`, `mem`, `stack`, ...). |
| **TUI debugger** | `./simulator --tui6 prog.s` | Full-screen interactive debugger. `--tui4` selects the 4-stage pipeline, `--tui` defaults to 6-stage. |

The batch and CLI-debugger modes behave exactly as before. The TUI can also be
opened from the interactive prompt after loading a program:

```text
risc201> load tests/test_stress.s
risc201> tui6          # or: tui4 / tui 4
```

### TUI debugger

The screen is deliberately three bands rather than a mosaic of panels:

```text
header      model, cycle count, run state
pipeline    one card per stage: stage name, current instruction, key value
main        active view (registers by default) + always-on state side panel
footer      status message / command line, then key hints
```

Keys:

| Key | Action |
| :--- | :--- |
| `s` / `space` | step one clock |
| `r` | run until a breakpoint or until the CPU stops |
| `f` | run to completion (ignores breakpoints) |
| `R` | reset and reload the program |
| `m` / `k` / `y` / `t` / `b` / `g` / `h` | memory, stack, system registers, stats, breakpoints, registers, help |
| `1`–`7` | same views by number; `Tab` cycles them |
| `↑` / `↓` / `PgUp` / `PgDn` | scroll the memory window |
| `:` | open a command line (full CLI-debugger command set) |
| `q` / `Ctrl-C` | quit (or interrupt a run) |

Commands accepted after `:` — `step [n]`, `run`, `finish`, `reset`,
`break <addr>`, `delete <addr>`, `breaks`, `regs`, `mem [addr]`,
`stack [n]`, `sysregs`, `stats`, `help`, `quit`.

Notes:

- Breakpoints are compared against the architectural PC, which this core keeps
  in instruction-index units (the same value `debug4`/`debug6` print).
- The memory and stack views read through L1 as well as main memory, so recent
  stores are visible before their cache line is written back.
- The TUI requires an interactive terminal; if stdin/stdout is redirected it
  exits with a message and suggests `--debug4` / `--debug6`.
- `make test` also runs `make test-tui`, the terminal-free tests for the TUI
  renderer and model.

### Inspecting the two control units

The interactive simulator can display the same pipeline through either control
unit. Both commands accept an optional width (`4`/`6`), a cycle count to advance
first, `run` to advance to completion, and `reset` to restart; `micropipeline`
also accepts `all` to dump the whole microprogram store.

```text
risc201> load tests/test_basic.s
risc201> pipeline 4 5         # control-ROM latches + control signals + ROM listing
risc201> micropipeline 4 5    # μCEU latches + microPC/MIR/bus + microprograms
risc201> pipeline 6 4         # same views on the 6-stage pipeline
risc201> micropipeline 6 all  # full microprogram store
```

`pipeline` replays the production `FourStageCPU`/`SixStageCPU`, so its latch
behaviour matches `debug4`/`debug6`. `micropipeline` drives the separate
`mc::MicroprogrammedCPU` model from `src/microcontrol/`, whose per-latch
microcontrol execution units expose `microPC`, `MIR` and shared-bus state.

## Report

The full design rationale, hazard analysis, test results and cycle-level measurements are in
[`COA Project Report.pdf`](COA%20Project%20Report.pdf).

## Team

Dhyaan Shah(25114032) · Hitansh Gandhi(25114044) · Nitesh Anand(25114065) · Ritik Yadav(25114079) · Vansh Gupta(25114097)
