; TEST 5: ecall / eret round trips (handler is a single eret)
; Handler placement depends on your trap vector: adjust the first lines to match it.
; With this layout N = 21 and the initial 'b main' adds 2 stall cycles:
; expected stalls: 62 (4-stage), 82 (6-stage)

b main
handler:
eret
main:
ecall
ecall
ecall
ecall
ecall
ecall
ecall
ecall
ecall
ecall
