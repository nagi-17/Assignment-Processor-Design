mov r1, r5           ; Should expand to: add r1, r5, r12
mov r2, r6           ; Should expand to: add r2, r6, r12
    
push r1              ; Should expand to: sub r14, r14, 4 -> st r1, r14, 0
push r2              
    
pop r3               ; Should expand to: ld r3, r14, 0 -> add r14, r14, 4
    
ld r4, r13, 8        ; Standard I-Type memory load
wfi