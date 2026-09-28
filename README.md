Finalised instructions : 
1. Arithematic (6) : add,, sub, mul, div, cmp, mod - these will have their own opcode and will be implemented in HARDWARE logic
2. Logical (3) : and, or, not- these will have their own opcode and will be implemented in HARDWARE logic
3. Shift (3) : lsl, lsr, asr - these will have their own opcode and will be implemented in HARDWARE logic
4. Data transfer (2) : ld, st - these will have their own opcode and will be implemented in HARDWARE logic
5. Branch (4) : b, beq, bgt, blt - these will have their own opcode and will be implemented in HARDWARE logic
6. call and ret (2) - these will have their own opcode and will be implemented in HARDWARE logic
7. nop (1) - this will have it's own opcode and will be implemented in HARDWARE logic
8. mov (1) - this will not have a opcode, it will be implemented as a software alias, what that means is that `mov` will be tweaked to `addi` in the lookup table in assembler

Instructions which we can think about : 
1. `ebreak <address>` : now this instruction will help us in implementing interactive CLI debugger, so basically what this instruction does is that whenever our program/CPU encounters this instruction in the instruction stream, then it will halt the execution, save current PC (and maybe more things/regs/data), raise a `BREAKPOINT_EXECEPTION` (if req) and pass the control to the interactive CLI prompt so that the user can use debugging features to inspect current state of registers or of execution pipeline - implement in HARDWARE
2. `clrz <reg_name>` and/or `clrz.all` : securely wipes (makes 0) specified or all registers
3. `chk <reg_name_1> <reg_name_2>` (safety check/bound check) : checks if `<reg_name_1> >= <reg_name_2>`, if yes then it throws a `OUT_OF_BOUNDS` exception
These are all related/(maybe)required at system/kernel level : 
4. `syscall`/`ecall` (Environment call) : instruction for software-triggered interrupt (in thsi ig we will have to use some extra specific registers)
5. `eret` (exception return) : basically whenever exception handler completes resolving any issue, then it should return to the user program, so this is done by storing PC (in a system register EPC which can't be accessed directly by user)
6. `csrr` and `csrw` : these instructions are required by system, as the general registers can't like kinda talk with system registers, so basically we need specific instructions to do this
7. `wfi` : it basically makes the processor "sleep" until any external event is triggered