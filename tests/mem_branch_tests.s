.globl _start
_start:
    # Memory and branch tests
    lui x1, 1            # base addr = 1 << 12 = 0x1000 (avoid overlapping .text)
    addi x2, x0, 42
    sw x2, 0(x1)         # store word
    lw x3, 0(x1)         # load word -> x3 == 42

    # Branch: loop forward/backward
    addi x4, x0, 0
    addi x5, x0, 3
loop:
    addi x4, x4, 1
    blt x4, x5, loop

    ori a7, x0, 93
    ecall
