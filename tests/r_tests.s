.globl _start
_start:
    # R-type instruction tests: add, sub, and, or, xor, sll, srl, sra, slt, sltu
    addi x1, x0, 5
    addi x2, x0, 3
    add x3, x1, x2      # x3 = 8
    sub x4, x1, x2      # x4 = 2
    and x5, x1, x2
    or  x6, x1, x2
    xor x7, x1, x2
    sll x8, x1, x2
    srl x9, x1, x2
    sra x10, x1, x2
    slt x11, x2, x1
    sltu x12, x2, x1
    # finish via ecall (exit)
    ori a7, x0, 93
    ecall
