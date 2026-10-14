# Initialization
    add r1, r0, 10       ; I-Type: Last operand is a number
    add r2, r0, 20       ; I-Type

# Arithmetic 
    add r3, r1, r2       ; R-Type: Last operand is a register
    sub r4, r3, 5        ; I-Type
    mul r5, r3, r4       ; R-Type

# System
    nop
    ebreak