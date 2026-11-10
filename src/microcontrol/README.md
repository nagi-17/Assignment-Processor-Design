# RISC201 Microcontrol Model

This directory is a **standalone implementation** of the per-latch microcontrol
execution unit (μCEU) design.  It deliberately does not modify the production
pipeline sources.  The model reuses the project's ALU, FPU, register file,
control/status unit and ISA table, but owns its latches, microcode store,
microsequencer, shared buses and simple data memory.

```
make -f Makefile.microcontrol
./microcontrol_simulator tests/microcontrol/example.txt --4
./microcontrol_simulator tests/microcontrol/example.txt --6
./microcontrol_simulator tests/microcontrol/example.txt --6 --trace
make -f Makefile.microcontrol test
```

## Why a separate model

The production simulator already has a horizontal control word per instruction.
This model takes the next step required by the assignment: every pipeline latch
is backed by a real microcontrol execution unit that fetches a stage-local
microprogram from control memory and drives a shared bus.

A **single global shared bus** was rejected: it would serialize IF, ID, EX and
MEM and destroy simultaneous multi-stage execution.  SimpleRisc can use one bus
because that machine is unpipelined.  Here every stage owns its own 32-bit bus,
so several instructions can be in different stages in the same clock.

```
          IF/ID               ID/EX                    EX/MA1 (or EX/MEM)      MA1/MA2      MA2/WB
 PC ──► [IF μCEU] ──► latch ──► [ID μCEU] ──► latch ──► [EX μCEU] ──► latch ──► [MA1 μCEU] ──► latch ──► [WB μCEU] ──► RF
          bus                   bus                    bus                    bus                  bus
      PC, IMEM              RF ports, extender      ALU, FPU, AGU           MAR, memory          WB mux, RF write
```

The microsequence runs **inside one pipeline clock** and commits once on
`Seq::Done`.  The pipeline therefore still advances one stage per clock, while
`microPC` is visible for step-by-step inspection.

## Files

| File | Purpose |
| :--- | :--- |
| `microinstruction.hpp` | Microinstruction, bus-driver / destination / functional-unit enums. |
| `shared_bus.hpp` | 32-bit single-driver bus with conflict detection. |
| `microsequencer.hpp` | microPC, MIR, repeat counter, `Next`/`Repeat`/`Done`. |
| `microcontrol_execution_unit.hpp` | Abstract μCEU: store lookup, bus, sequencer, `apply()`/`commit()`. |
| `microprogram_store.hpp/.cpp` | The declarative microcode listing for every stage and opcode. |
| `fetch_micro_unit.hpp` | IF μCEU. |
| `decode_micro_unit.hpp` | ID μCEU and `peek()` for the hazard unit. |
| `execute_micro_unit.hpp` | EX μCEU, multi-cycle scheduling, ALU/FPU/CSR/branch/trap. |
| `memory_micro_unit.hpp` | MA1, MA2, WB and the 4-stage merged MEM/WB μCEUs. |
| `pipeline_latches.hpp` | New latches carrying `micro_key`, plus instruction memory. |
| `microprogrammed_cpu.hpp/.cpp` | 4-stage and 6-stage pipelined model. |
| `microcontrol_main.cpp` | CLI and batch runner. |
| `Makefile.microcontrol` | Separate build; production Makefile untouched. |

## Microinstruction format

A microinstruction is one micro-op.  It names one bus source, any number of
destination registers, and an optional functional-unit command.

| Field | Width | Meaning |
| :--- | :--- | :--- |
| `src` | 8 | Bus driver (`Pc`, `RegA`, `RegB`, `Imm`, `AluOut`, `Mdr`, `CsrOut`, IR-field extractors, `RfA/RfB`, `ImemOut`, ...). |
| `dests` | 32 | Bitmask of latch destinations (`D_ALU_A`, `D_ALU_B`, `D_MAR`, `D_RESULT`, `D_RF_WRITE`, ...). |
| `alu_op` | 4 | ALU operation when `src == AluOut`. |
| `fpu_op` | 3 | FPU operation when `src == FpuOut`. |
| `mem_op` | 2 | `Probe`, `Read`, `Write`. |
| `branch` | 3 | `Eq`, `Ne`, `Gt`, `Lt`, `Always`. |
| `effect` | 3 | `Chk`, `CsrWrite`, `Trap`, `Link`. |
| `seq` | 2 | `Next`, `Repeat`, `Done`. |
| `label` | — | Human-readable label used by the debugger. |

The 40-bit horizontal `ControlWord` is projected from the listing for display
and for the `peek()` hazard rules, so the original `IS_ADD`, `IS_WB`, `IS_LD`,
`IS_BEQ` etc. names remain available.

## Internal register map (derived from this codebase)

### Architectural / system

| Register | Width | Owner |
| :--- | :--- | :--- |
| `PC` (instruction index) | 32 | `FetchMicroControlUnit::pc_` |
| `IR` | 32 | IF/ID latch / fetch bank |
| `RF[0..15]` (r12 = 0) | 16×32 | `RegisterFile` |
| `flags.E`, `flags.GT` | 1 each | `ControlStatusUnit` |
| `epc`, `cause` | 32 each | `ControlStatusUnit` |
| `halted`, `in_trap`, `trap_vector` | 1/1/32 | `ControlStatusUnit` |
| `pending`, `pending_cause`, `pending_epc` | 1/32/32 | `ControlStatusUnit` |

### IF and IF/ID

| Register | Width | Field |
| :--- | :--- | :--- |
| `PC` | 32 | `pc_` |
| `IMEM_ADDR` | 32 | `imem_addr_` |
| `IMEM_DATA / IR` | 32 | `ir_` |
| `PC_NEXT` | 32 | `pc_next_` |
| `IF/ID.PC`, `IF/ID.IR`, `IF/ID.PC_NEXT`, `IF/ID.Valid` | 32/32/32/1 | `LatchIFID` |

### ID and ID/EX

| Register | Width | Field |
| :--- | :--- | :--- |
| `ID.IR` | 32 | `DecodeBank::ir` |
| `ID.RD`, `ID.RS1`, `ID.RS2` | 4 each | `DecodeBank::rd/rs1/rs2` |
| `ID.A`, `ID.B` | 32 each | `DecodeBank::a/b` |
| `ID.IMM` | 32 | `DecodeBank::imm` |
| `ID.RF_A`, `ID.RF_B` (read ports) | 32 each | `RfA`/`RfB` bus sources |
| `ID.ControlWord` | 40 | `DecodeBank::control` |
| `ID/EX.MicroKey` | 16 | `LatchIDEX::micro_key` |
| `ID/EX.PC_NEXT`, `RD`, `A`, `B`, `IMM`, `Control`, `Valid` | — | `LatchIDEX` |

### EX and EX/MEM

| Register | Width | Field |
| :--- | :--- | :--- |
| `EX.A`, `EX.B`, `EX.IMM` | 32 each | `ExBank::a/b/imm` |
| `ALU_A`, `ALU_B`, `ALU_OUT` | 32 each | `ExBank::alu_a/alu_b/alu_out` |
| `FPU_A`, `FPU_B`, `FPU_OUT` | 32 each | `ExBank::fpu_a/fpu_b/fpu_out` |
| `RESULT` (ALU/FPU/CSR/link) | 32 | `ExBank::result` |
| `ST_DATA` | 32 | `ExBank::st_data` |
| `MAR` (address generated in EX) | 32 | `ExBank::mar` |
| `BRANCH_TARGET`, `BRANCH_TAKEN` | 32/1 | `ExBank` |
| `CSR_IN`, `CSR_OUT` | 32 each | `ExBank` |
| `COND_E`, `COND_GT` (flags snapshot) | 1 each | `ExBank` |
| `FLAG_WRITE`, `FLAG_EQUAL`, `FLAG_GREATER` | 1 each | `ExBank` |
| `STACK_FAULT` | 1 | `ExBank` |
| `MULTI.BUSY`, `MULTI.REMAINING`, `MULTI.HELD` | 1/32/latch | `ExecuteMicroControlUnit` |
| `EX/MEM.*` | — | `LatchMem` |

### MA1 / MA2 / WB

| Register | Width | Field |
| :--- | :--- | :--- |
| `MAR` | 32 | `Ma1Bank::mar` |
| `M1_HIT` | 1 | `Ma1Bank::m1_hit` |
| `MDR` | 32 | `Ma2Bank::mdr` |
| `READ_DATA` | 32 | `Ma2Bank::read_data` / `LatchMem::read_data` |
| `STALL_CYCLES` | 32 | `Ma2MicroControlUnit::clock` out-param |
| `MA1/MA2.*`, `MA2/WB.*` | — | `LatchMem` |
| `WB.WRITE_DATA`, `WB.WE`, `WB.RD` | 32/1/4 | `WbBank` + `LatchMem::rd` |

### μCEU control registers (every stage)

| Register | Width | Purpose |
| :--- | :--- | :--- |
| `microPC` | 8 | Index into the current microprogram. |
| `MIR` | struct | Current microinstruction. |
| `repeatRemaining` | 8 | Repeat counter. |
| `busDriver`, `busValue` | 8/32 | Last shared-bus transfer (debug). |
| `lastTrace` | vector | Micro-ops executed in the last clock. |

### Hazard snapshots

The CPU snapshots `ex_mem_` and `ma1_ma2_` before the memory stages shift them,
so the interlock sees the instructions that are actually still in MA1/MA2.  WB
is excluded because it writes the register file before ID in the step order.

## Microprogram examples

`add r3, r1, r2` (R format):

```
ID  : IR[rd]->RD, IR[rs1]->RS1, IR[rs2]->RS2, RF[rs1]->A, RF[rs2]->B, commit
EX  : A->ALU_A, B->ALU_B, ALUOut(ADD)->RESULT, commit
WB  : EX_RESULT->RF, commit
```

`ld r3, 8(r1)` (I format):

```
ID  : IR[rd]->RD, IR[rs1]->RS1, RF[rs1]->A, IR[imm]->IMM, commit
EX  : A->ALU_A, IMM->ALU_B, ALUOut(ADD)->RESULT,MAR, commit
MA1 : EX_RESULT->MAR, tag probe, commit
MA2 : memory read, MDR->READ_DATA, commit
WB  : READ_DATA->RF, commit
```

`mul r3, r1, r2` (multi-cycle):

```
EX  : A->ALU_A, B->ALU_B, ALUOut(MUL)->RESULT, commit
      first 3 EX clocks are bubbles; the 4th commits the result
```

`beq offset`:

```
ID  : IR[offset]->IMM, commit
EX  : PC+offset->BRANCH_TARGET, evaluate EQ, commit
```

`call offset` / `ret`:

```
call EX : PC+1->RESULT (link), PC+offset->BRANCH_TARGET, always taken, commit
ret  EX : RF[ra]->BRANCH_TARGET, always taken, commit
```

## Pipeline behavior

| Model | Stages |
| :--- | :--- |
| 4-stage | IF, ID, EX, MEM/WB |
| 6-stage | IF, ID, EX, MA1, MA2, WB |

- The EX unit keeps the production latencies: `mul` 4, `div/mod` 16, `fmul` 4,
  `fdiv` 6.  Bubbles are emitted on the first N-1 cycles and the result commits
  on cycle N.
- The hazard unit is a conservative interlock (no forwarding): a consumer in ID
  waits while EX, MA1 or MA2 still holds its producer.  This is intentionally
  simple for the model; the production CPU's forwarding path is unchanged and
  still lives in the original sources.
- Traps use the existing `ControlStatusUnit`: `ecall` / `chk` redirect to the
  trap vector, `eret` returns to `epc`, `wfi` stops the model, `ebreak` halts at
  WB.
- Data memory is a simple word-addressed `std::map`; the production cache
  hierarchy is not duplicated here.  Consequently, cycle counts are not
  bit-identical to the production CPU, but the micro-sequence and EX latency
  behavior are.

## CLI

```
load <file>     load a .bin (32-bit binary words) or .hex program
run4 / run6     run the program on the 4/6-stage model
step4 / step6   advance one clock and print latches + microPCs
regs            register file
pc              fetch PC
pipeline        pipeline latches
micro           every stage's microPC, MIR, bus driver and bus value
trace           the last clock's micro-op sequence
mem <addr>      read a data-memory word
cycles          cycle / stall / retired statistics
reset           reset the current model
help / quit
```

`tests/microcontrol/example.txt` exercises `movu`, `add`, `mul`, `st` and `ld`
on both pipelines.

## Integration with the main simulator

The model is also linked into the production `./simulator` (see
`micro_session.cpp` and `EngineSession`), which adds two inspection commands at
the `risc201>` prompt:

```
pipeline [4|6] [n|run|reset]        control-ROM pipeline + control ROM listing
micropipeline [4|6] [n|run|reset|all]  μCEU pipeline + microprograms
```

The `pipeline` view replays the production CPU and shows its horizontal control
word; `micropipeline` shows this model's per-stage `microPC`, `MIR`, bus and
micro-op trace. `microcontrol_main.cpp` stays out of the simulator build so the
two CLIs keep separate `main()` functions.

## Integration notes

Nothing in `src/headers`, `src/simulator`, `src/debugger`, the assembler, the
disassembler, the original `Makefile` or the existing tests was modified.  To
fold this model into the production simulator later:

1. Add a `micro_key` (and optional `micro_pc`) field to the production latches.
2. Make `InstructionFetchStage`, `OperandFetchStage`, `ExecuteStage` and the
   memory stages delegate to the corresponding μCEUs, keeping their existing
   public method signatures.
3. Add the per-stage μCEU state to `DebuggerSnapshot` / `PipelineStageSnapshot`
   and expose the `micro`/`microtrace` commands in `debugger_cli.cpp`.
4. Keep the existing `ControlWord`/`Signal` projection so the current debugger
   and tests continue to compile.

The new headers are independent of `four_stage_latches.hpp` /
`six_stage_latches.hpp`, so they can be included in either pipeline build.
