# ==============================================================================
# RISC201 Test 2: IEEE-754 FPU Arithmetic, Comparisons & Register Aliasing
# Explores:
#   1. IEEE-754 Single-Precision Floating-Point Unit (binary32)
#   2. Register Aliasing: f0-f3 alias r8-r11 (allowing float & int interop)
#   3. FPU Operations: fmov, fmul, fadd, fdiv, fsub, fcmp
#   4. FPU comparisons updating flags.E / flags.GT for conditional branches
# ==============================================================================

main:
    # Load 32-bit floating point immediate constants into aliased float registers
    # f0 (r8) = 2.5, f1 (r9) = 4.0, f2 (r10) = 1.5, f3 (r11) = 0.5
    fmov f0, 2.5
    fmov f1, 4.0
    fmov f2, 1.5
    fmov f3, 0.5

    # Compute scaled coordinates:
    # f0 = f0 * 1.5  (2.5 * 1.5 = 3.75)
    fmul f0, f0, f2

    # f1 = f1 * 0.5  (4.0 * 0.5 = 2.0)
    fmul f1, f1, f3

    # Sum coordinates:
    # f0 = 3.75 + 2.0 = 5.75
    fadd f0, f0, f1

    # Divide by 2.0 to compute average:
    # First set f2 to 2.0
    fmov f2, 2.0
    # f0 = 5.75 / 2.0 = 2.875
    fdiv f0, f0, f2

    # Compare with reference threshold 2.5 (stored in f1)
    fmov f1, 2.5
    fcmp f0, f1                  ; Compare f0 with f1 (2.875 > 2.5 -> flags.GT set)

    # Branch on greater-than (bgt)
    bgt valid_range
    ebreak                       ; Fail if not greater

valid_range:
    nop
    ebreak                       ; Completed successfully
