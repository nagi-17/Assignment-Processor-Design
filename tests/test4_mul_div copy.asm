; TEST 4: multi-cycle ALU operations (immediate forms, r1 is never written)
; N = 28, expected stalls: 20*3 + 8*15 = 180 (both pipelines)

mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
mul r0, r1, 7
div r3, r1, 3
div r3, r1, 3
div r3, r1, 3
div r3, r1, 3
div r3, r1, 3
div r3, r1, 3
div r3, r1, 3
div r3, r1, 3
