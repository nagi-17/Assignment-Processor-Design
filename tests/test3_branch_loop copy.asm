; TEST 3: tight branch loop (10 iterations)
; N = 42, taken branches = 9, expected stalls: 18 (both pipelines)

add r2, r12, 10
add r0, r12, 0
loop:
add r0, r0, 1
add r1, r1, r0
cmp r0, r2
blt loop
