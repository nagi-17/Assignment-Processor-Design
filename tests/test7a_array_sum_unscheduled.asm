; TEST 7a: array sum, 20 elements, load immediately used
; N = 103, expected stalls: 58 (4-stage), 78 (6-stage)

add r4, r12, 0
add r5, r12, 80
add r0, r12, 0
loop:
    ld  r1, r4, 0
    add r0, r0, r1
    add r4, r4, 4
    cmp r4, r5
    blt loop
