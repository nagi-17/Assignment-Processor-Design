# RISC201 Architecture Specification

### Registers (32-bit Unified File)
- `r0-r3`: general purpose int registers
- `r4-r7`: scratch registers (Caller-saved)
- `r8-r11`: general purpose float registers
- `r12`: zero register (Hardwired to 0)
- `r13`: frame pointer (points to the top of the activation block)
- `r14`: stack pointer
- `r15`: return address
- **System Registers (Inaccessible directly by the programmer):**
  - `pc`
  - `flags.E` and `flags.GT` (1-bit)
  - `epc`: exception program counter
  - `cause`: exception cause code

### Instruction Encoding Formats
- **Register Format (R-Type):**
  ```
  [6-bit opcode] [1-bit I=0] [4-bit rd] [4-bit rs1] [4-bit rs2] [13-bit padding]
  ```
- **Immediate Format (I-Type):**
  ```
  [6-bit opcode] [1-bit I=1] [4-bit rd] [4-bit rs1] [16-bit imm] [1-bit padding]
  ```
- **Float Immediate Format (F-Type - specifically for `fmov`):**
  ```
  [6-bit opcode] [1-bit I=1] [4-bit rd] [21-bit float imm]
  ```

### Instruction Set Architecture (ISA)

| Instruction | Opcode | Format | Operands | Description |
| :--- | :--- | :--- | :--- | :--- |
| **Integer ALU inst. (00)** | | | | |
| `nop` | `000000` | R | None | No operation |
| `add` | `000001` | R / I | `rd, rs1, rs2/imm` | |
| `sub` | `000010` | R / I | `rd, rs1, rs2/imm` | |
| `mul` | `000011` | R / I | `rd, rs1, rs2/imm` | |
| `div` | `000100` | R / I | `rd, rs1, rs2/imm` | |
| `mod` | `000101` | R / I | `rd, rs1, rs2/imm` | |
| `cmp` | `000110` | R / I | `rs1, rs2/imm` | |
| `and` | `001000` | R / I | `rd, rs1, rs2/imm` | |
| `or` | `001001` | R / I | `rd, rs1, rs2/imm` | |
| `xor` | `001010` | R / I | `rd, rs1, rs2/imm` | |
| `not` | `001011` | R / I | `rd, rs1` | |
| `lsl` | `001100` | R / I | `rd, rs1, rs2/imm` | |
| `lsr` | `001101` | R / I | `rd, rs1, rs2/imm` | |
| `asr` | `001110` | R / I | `rd, rs1, rs2/imm` | |
| **FPU inst. (01)** | | | | |
| `fadd` | `010001` | R | `rd, rs1, rs2` | Floating Addition (IEEE-754). |
| `fsub` | `010010` | R | `rd, rs1, rs2` | Floating Subtraction. |
| `fmul` | `010011` | R | `rd, rs1, rs2` | Floating Multiplication. |
| `fdiv` | `010100` | R | `rd, rs1, rs2` | Floating Division. |
| `fcmp` | `010110` | R | `rs1, rs2` | Floating Compare: Updates standard flags. |
| `fmov` | `011000` | F | `rd, imm21` | Loads a 21-bit immediate floating-point value. |
| **Memory & System inst. (10)**| | | | |
| `ld` | `100000` | I | `rd, rs1, imm` | |
| `st` | `100001` | I | `rs2, rs1, imm` | |
| `movu` | `100010` | I | `rd, imm` | Move Upper: Loads immediate into top 16 bits of `rd`. |
| `movh` | `100011` | I | `rd, imm` | Move Half: Loads immediate into bottom 16 bits of `rd`. |
| `csrr` | `101000` | I | `rd, csr_imm` | Read system register specified by `csr_imm` into `rd`. |
| `csrw` | `101001` | I | `csr_imm, rs1` | Write `rs1` into system register specified by `csr_imm`. |
| `chk` | `101100` | R | `rs1, rs2` | Bounds Check: Traps if `rs1 >= rs2`. |
| `clrz` | `101101` | R | `rd` | Zeroize: Securely wipes the specified register (`rd = 0`). |
| **Control Flow inst. (11)** | | | | |
| `b` | `110000` | I | `imm` | |
| `beq` | `110001` | I | `imm` | |
| `bne` | `110010` | I | `imm` | |
| `bgt` | `110011` | I | `imm` | |
| `blt` | `110100` | I | `imm` | |
| `call` | `111000` | I | `imm` | |
| `ret` | `111001` | R | None | |
| `ecall` | `111100` | R | None | Triggers environment trap/syscall to OS or simulator. |
| `eret` | `111101` | R | None | Exception Return: Restores `PC` from `epc` register. |
| `ebreak` | `111110` | R | None | Breakpoint: Halts execution, drops to CLI debugger. |
| `wfi` | `111111` | R | None | Wait for Interrupt: Pauses execution until external event. |

*Note: `push` and `pop` are implemented as assembler preprocessor macros*

### Instructions which we are not sure about : 
1. `ebreak <address>` : now this instruction will help us in implementing interactive CLI debugger, so basically what this instruction does is that whenever our program/CPU encounters this instruction in the instruction stream, then it will halt the execution, save current PC (and maybe more things/regs/data), raise a `BREAKPOINT_EXECEPTION` (if req) and pass the control to the interactive CLI prompt so that the user can use debugging features to inspect current state of registers or of execution pipeline - implement in HARDWARE
2. `clrz <reg_name>` and/or `clrz.all` : securely wipes (makes 0) specified or all registers
3. `chk <reg_name_1> <reg_name_2>` (safety check/bound check) : checks if `<reg_name_1> >= <reg_name_2>`, if yes then it throws a `OUT_OF_BOUNDS` exception
These are all related/(maybe)required at system/kernel level
4. `syscall`/`ecall` (Environment call) : instruction for software-triggered interrupt (in this ig we will have to use some extra specific registers)
5. `eret` (exception return) : basically whenever exception handler completes resolving any issue, then it should return to the user program, so this is done by storing PC (in a system register EPC which can't be accessed directly by user)
6. `csrr` and `csrw` : these instructions are required by system, as the general registers can't like kinda talk with system registers, so basically we need specific instructions to do this
7. `wfi` : it basically makes the processor "sleep" until any external event is triggered
