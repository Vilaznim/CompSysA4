.globl _start
_start:
    # M-extension tests: mul, mulh, mulhu, div, divu, rem, remu
    addi x1, x0, 7
    addi x2, x0, 3
    mul x3, x1, x2
    mulh x4, x1, x2
    mulhu x5, x1, x2
    div x6, x1, x2
    divu x7, x1, x2
    rem x8, x1, x2
    remu x9, x1, x2
    ori a7, x0, 93
    ecall
