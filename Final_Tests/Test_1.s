# ==============================================================================
# RISC201 Test 1: Nested Subroutine Calls & the Mandatory Stack
#
# Problem: Compute a^2 + b^2  for  a = 3, b = 4   (expected: 25)
#
# Why the stack is unavoidable here:
#   square_sum() calls square() twice. The 'call' instruction overwrites the
#   link register (ra / r15) with the return address each time it fires.
#   If square_sum() does NOT push ra before calling square(), the return
#   address back to main is destroyed and the CPU can never return correctly.
#
#   The push/pop of ra inside square_sum() is not stylistic — it is the only
#   mechanism that keeps two levels of call/ret from clobbering each other.
#
# Features exercised:
#   - 'call' + 'ret'  : link register (ra/r15) written by hardware
#   - 'push' / 'pop'  : assembler macros -> (sub sp,4 ; st) and (ld ; add sp,4)
#   - 'mul'           : Radix-4 Booth multiplier (multi-cycle, stalls EX)
#   - Nested calls    : prove that only stack-saved ra survives two call levels
#   - sp / zero       : register aliases wired into hardware
# ==============================================================================

main:
    add sp, zero, 200       ; Initialise stack pointer at byte address 200

    add r1, zero, 3         ; Argument a = 3
    add r2, zero, 4         ; Argument b = 4

    call square_sum         ; r0 = square_sum(3, 4) = 3^2 + 4^2 = 9 + 16 = 25

    add r4, zero, 25        ; Expected result
    cmp r0, r4
    beq passed
    ebreak                  ; Trap if result is wrong

passed:
    nop
    ebreak                  ; Success

# ==============================================================================
# square_sum(r1 = a, r2 = b)  ->  r0 = a^2 + b^2
#
# This function MUST save ra before calling square(), because 'call square'
# overwrites ra with the return address into square_sum. Without saving it
# first, the subsequent 'ret' would jump back into square_sum, not into main.
# ==============================================================================
square_sum:
    nop
    nop
    nop
    nop
    push ra                 ; [CRITICAL] save return-address to main on stack
    push r2                 ; save b — it must survive the first call to square

    ; --- first call: compute a^2 (r1 already holds a) ---
    call square             ; r0 = a^2 ;  ra is now square_sum's return point
    push r0                 ; save a^2 on stack before second call overwrites r0

    ; --- second call: compute b^2 ---
    ; b was saved at sp+4 (above the a^2 we just pushed); peek without popping
    ld r1, sp, 4            ; r1 = b (reload b as argument for square)
    call square             ; r0 = b^2

    ; --- combine results ---
    pop r4                  ; r4 = a^2 (restore from stack)
    add r0, r0, r4          ; r0 = a^2 + b^2 = 25

    pop r2                  ; restore b (stack discipline)
    pop ra                  ; restore return address back to main
    nop
    nop
    ret

# ==============================================================================
# square(r1 = x)  ->  r0 = x * x
#
# A pure leaf function: it makes no calls, so it never touches ra or the stack.
# ==============================================================================
square:
    mul r0, r1, r1          ; r0 = x^2  (Radix-4 Booth Multiplier, multi-cycle)
    ret
