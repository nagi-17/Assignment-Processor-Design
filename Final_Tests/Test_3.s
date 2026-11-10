# ==============================================================================
# RISC201 Test 3: Hardware Security, Stack Guard & Control/Status Unit (CSU)
# Explores:
#   1. Hardware Stack Boundary Check: 'chk sp, bound'
#      (raises hardware trap in MA1/EX if SP exceeds allowable boundary)
#   2. Control and Status Registers (CSR):
#      - csrr rd, csr_index  (read CSR)
#      - csrw csr_index, rs  (write CSR)
#      - CSR 0 = flags (E, GT), CSR 2 = cause
#   3. Hardware register zeroing: 'clrz rd'
#   4. System breakpoint: 'ebreak'
# ==============================================================================

main:
    # 1. Setup simulated stack pointer and boundary
    add r1, zero, 100            ; r1 = Current Stack Pointer (100)
    add r2, zero, 256            ; r2 = Stack Upper Limit (256)

    # 2. Hardware Stack Boundary Check
    # chk verifies r1 < r2. If r1 >= r2, CPU triggers OutOfBounds fault
    chk r1, r2                   ; Pass: 100 < 256 (no fault raised)

    # 3. Test Comparison and Flags CSR
    add r3, zero, 42
    add r4, zero, 42
    cmp r3, r4                   ; 42 == 42, sets flags.E (bit 0 of CSR 0)

    # Read Flags CSR (CSR 0) into r5
    csrr r5, 0                   ; r5 should have bit 0 set (equal flag)

    # 4. Test User CSR write & readback (writing cause register CSR 2)
    add r6, zero, 7              ; Custom diagnostic status code
    csrw 2, r6                   ; Write code 7 into CSR 2
    csrr r7, 2                   ; Read back from CSR 2 into r7

    # 5. Hardware Zeroing instruction
    clrz r6                      ; Hardware resets r6 to 0
    cmp r6, zero
    beq all_passed
    ebreak                       ; Fail if r6 != 0

all_passed:
    nop
    ebreak                       ; Success breakpoint
