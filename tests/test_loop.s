main:
    add r1, r0, 5        ; Loop counter
    add r2, r0, 0        ; Accumulator
    
loop_start:
    cmp r1, r0           ; Compare counter with 0
    beq r1, r0, end_prog ; Forward branch (Positive offset)
    
    add r2, r2, 1        ; Increment accumulator
    sub r1, r1, 1        ; Decrement counter
    b loop_start         ; Backward branch (Negative two's complement offset)

end_prog:
    csrw 255, r2         ; Test irregular I-Type (operand swap)
    clrz r2              ; Test irregular R-Type (1 operand)
    ret