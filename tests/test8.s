add r4, r12, 0      ; r4 = base address
add r5, r12, 80     ; r5 = base address + 80
add r0, r0, 0       ; initialize sum to 0

ld  r1, r4, 0       ; <-- Target instruction. 
add r0, r0, r1      
add r4, r4, 4       
cmp r4, r5          
blt -16              ; Jump back 4 instructions to 'ld'. Try -5 if PC already incremented.