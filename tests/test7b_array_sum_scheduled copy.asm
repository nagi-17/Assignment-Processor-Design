; TEST 7b: array sum, 20 elements, load result used two instructions later
; N = 103, expected stalls: 38 (both pipelines)

add r4, r12, 0
add r5, r12, 20
add r0, r12, 0
loop:
ld  r1, r4, 0
add r4, r4, 1
cmp r4, r5
add r0, r0, r1
blt loop
