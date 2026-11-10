# ==============================================================================
# RISC201 Test 4: Memory Access Patterns, Cache Hierarchy & Pipelined MA1/MA2
# Explores:
#   1. Memory Subsystem: Multi-level cache lines (L1 hits vs L2/L3 misses)
#   2. Load-after-Store and Read-after-Write (RAW) data dependencies
#   3. Performance difference between 4-stage (merged MEM/WB) and 6-stage
#      (decoupled MA1 address calculation and MA2 data access)
#   4. Pointers, indexing, and array accumulation loop
# ==============================================================================

main:
    # Base memory address at word boundary 64 (0x40)
    add r4, zero, 64             ; r4 = Base pointer
    add r0, zero, 1              ; r0 = Value to write (and increment)

    # 1. Store a sequence of 8 words into consecutive memory addresses
    st r0, r4, 0
    add r0, r0, 1
    st r0, r4, 4
    add r0, r0, 1
    st r0, r4, 8
    add r0, r0, 1
    st r0, r4, 12
    add r0, r0, 1
    st r0, r4, 16
    add r0, r0, 1
    st r0, r4, 20
    add r0, r0, 1
    st r0, r4, 24
    add r0, r0, 1
    st r0, r4, 28

    # 2. Accumulate the array back into r2 using an indexed loop
    add r1, zero, 0              ; r1 = Byte offset index (0, 4, 8, ... 28)
    add r2, zero, 0              ; r2 = Sum accumulator
    add r5, zero, 32             ; r5 = Termination bound (8 words * 4 bytes = 32)

accum_loop:
    add r6, r4, r1               ; r6 = effective address (base + offset)
    ld  r3, r6, 0                ; Load element from memory
    add r2, r2, r3               ; Accumulate: sum += element
    add r1, r1, 4                ; Advance offset by 4 bytes (1 word)
    cmp r1, r5                   ; Check if done
    blt accum_loop               ; Repeat until 8 elements accumulated

    # Expected sum of 1..8 is 36
    add r7, zero, 36
    cmp r2, r7
    beq sum_verified
    ebreak                       ; Trap if sum incorrect

sum_verified:
    nop
    ebreak                       ; Success
