; TEST 6: L1 miss stream (stride 64, assumes a 64-word line; change to your line size)
; N = 20, expected stalls: 10 * 99 = 990 (both pipelines)
; Assumes r4 = 0 at reset and addresses 0..576 are valid

ld  r1, r4, 0
add r2, r2, 1
ld  r1, r4, 64
add r2, r2, 1
ld  r1, r4, 128
add r2, r2, 1
ld  r1, r4, 192
add r2, r2, 1
ld  r1, r4, 256
add r2, r2, 1
ld  r1, r4, 320
add r2, r2, 1
ld  r1, r4, 384
add r2, r2, 1
ld  r1, r4, 448
add r2, r2, 1
ld  r1, r4, 512
add r2, r2, 1
ld  r1, r4, 576
add r2, r2, 1
