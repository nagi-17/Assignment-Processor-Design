# ==============================================================================
# RISC201 Comprehensive Stress Test (test_stress.s)
# 
# Exercises:
#   1. Immediate & Register Integer ALU (Add, Sub, Mul, Div, Mod, Logic, Shifts)
#   2. Back-to-Back RAW Data Hazards (Heavy pipeline stalls)
#   3. Full-Descending Stack Operations (push, pop, memory load/store)
#   4. Cache Hierarchy Exercising (L1 -> L2 -> L3 -> L4 -> Main Memory)
#   5. Subroutine Call and Return (call, ret using r15/ra)
#   6. Floating Point Unit (fmov, fadd, fsub, fmul, fdiv, fcmp)
#   7. Conditional Branch Loops (Forward & Backward jumps, CMP flags)
#   8. System & Security features (chk, clrz, csrr, csrw, ebreak)
# ==============================================================================

main:
    # --------------------------------------------------------------------------
    # Section 1: Stack & Frame Pointer Initialization
    # --------------------------------------------------------------------------
    movu r14, 0                     # Set sp to 0x1000
    movh r14, 4096
    mov r13, r14                    # Set fp = sp

    # --------------------------------------------------------------------------
    # Section 2: RAW Hazard Stress & Integer ALU
    # Back-to-back dependencies force maximum stall bubbles in both pipelines
    # --------------------------------------------------------------------------
    add r1, r0, 42                  # r1 = 42
    add r2, r1, 18                  # r2 = r1 + 18 = 60  (RAW hazard on r1)
    sub r3, r2, 10                  # r3 = r2 - 10 = 50  (RAW hazard on r2)
    mul r4, r3, r1                  # r4 = 50 * 42 = 2100 (RAW on r3 & r1)
    div r5, r4, 20                  # r5 = 2100 / 20 = 105 (RAW on r4)
    mod r6, r4, 17                  # r6 = 2100 % 17 = 9 (RAW on r4)

    # --------------------------------------------------------------------------
    # Section 3: Bitwise Logic and Barrel Shifter
    # --------------------------------------------------------------------------
    and r7, r1, 31                  # r7 = 42 & 31 = 10
    or  r8, r7, 5                   # r8 = 10 | 5 = 15
    xor r9, r8, r7                  # r9 = 15 ^ 10 = 5
    not r10, r9                     # r10 = ~5
    lsl r11, r9, 3                  # r11 = 5 << 3 = 40
    lsr r11, r11, 2                 # r11 = 40 >> 2 = 10
    asr r11, r11, 1                 # r11 = 10 >> 1 = 5

    # --------------------------------------------------------------------------
    # Section 4: Stack & Memory Cache Hierarchy Stress
    # Multiple pushes and pops will trigger L1 misses, line refills, and writebacks
    # --------------------------------------------------------------------------
    push r1                         # push 42
    push r2                         # push 60
    push r4                         # push 2100
    push r5                         # push 105

    # Direct memory store & load at offset
    st r4, r14, 4                   # Store 2100 at [sp + 4]
    ld r7, r14, 4                   # Load back into r7

    pop r6                          # r6 = 105
    pop r5                          # r5 = 2100
    pop r2                          # r2 = 60
    pop r1                          # r1 = 42

    # --------------------------------------------------------------------------
    # Section 5: Function Call & Return
    # Call stores link address in r15 (ra), ret jumps back to r15
    # --------------------------------------------------------------------------
    call math_subroutine            # Jump to subroutine, link in r15

    # --------------------------------------------------------------------------
    # Section 6: Floating Point Unit (FPU) Stress
    # --------------------------------------------------------------------------
    fmov f0, 10.5                   # Load IEEE-754 10.5 into f0 (r8)
    fmov f1, 2.5                    # Load IEEE-754 2.5 into f1 (r9)
    fadd f2, f0, f1                 # f2 = 10.5 + 2.5 = 13.0
    fsub f3, f0, f1                 # f3 = 10.5 - 2.5 = 8.0
    fmul f0, f2, f3                 # f0 = 13.0 * 8.0 = 104.0
    fdiv f1, f0, f2                 # f1 = 104.0 / 13.0 = 8.0
    fcmp f1, f3                     # Compare f1 and f3 (both 8.0 -> Equal flag = 1)

    # --------------------------------------------------------------------------
    # Section 7: Conditional Loop Stress
    # Computes triangular sum: sum = 5 + 4 + 3 + 2 + 1 = 15
    # --------------------------------------------------------------------------
    add r1, r0, 5                   # Loop counter = 5
    add r2, r0, 0                   # Accumulator sum = 0

stress_loop:
    cmp r1, r0                      # Compare counter with 0
    beq r1, r0, loop_done           # If counter == 0, exit loop
    add r2, r2, r1                  # sum += counter
    sub r1, r1, 1                   # counter--
    b stress_loop                   # Jump back

loop_done:
    # --------------------------------------------------------------------------
    # Section 8: System & Security Instructions
    # --------------------------------------------------------------------------
    csrw 1, r2                      # Write sum (15) into EPC CSR (csr 1)
    csrr r7, 1                      # Read EPC CSR back into r7 (should be 15)
    clrz r8                         # Secure zeroing: r8 = 0
    chk r1, r2                      # Bounds check: checks r1 >= r2 (0 >= 15 is false -> no trap)

    # Clean termination
    ebreak                          # Halt execution, trap to debugger

# ------------------------------------------------------------------------------
# Subroutine: Doubler function
# Inputs: r1
# Output: r3 = r1 + r1
# ------------------------------------------------------------------------------
math_subroutine:
    add r3, r1, r1                  # r3 = 42 + 42 = 84
    ret                             # Return to caller via r15 (ra)
