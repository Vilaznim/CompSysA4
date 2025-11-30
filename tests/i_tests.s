.globl _start
_start:
    # I-type instruction tests: addi, andi, ori, xori, slti, slli, srli, srai
    addi x1, x0, -1      # sign-extended immediate
    addi x2, x0, 2
    andi x3, x1, 1
    ori  x4, x1, 1
    xori x5, x1, 3
    slti x6, x1, 0
    slli x7, x2, 1
    srli x8, x2, 1
    # srai: arithmetic right shift of negative value
    addi x9, x0, -8
    srai x10, x9, 2
    ori a7, x0, 93
    ecall
