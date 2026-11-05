# Initialization and FPU Macro Test
    fmov f0, 3.14        ; Should expand to movu and movh with IEEE-754 halves
    fmov f1, -2.5        ; Test with a negative float

# Standard FPU Operations (Assuming these exist in your ISA config)
    fadd f2, f0, f1
    fmul f3, f2, f0

# Mix with other macros to ensure pipeline stability
    push r1
    mov r2, r3           ; Should expand to add r2, r3, r12
    pop r1

# Label and branching to verify Pass 1/Pass 2 offset math
loop_start:
    cmp r1, r2
    bne r1, r2, loop_start
    ebreak