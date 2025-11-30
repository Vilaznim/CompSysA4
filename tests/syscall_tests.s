.globl _start
_start:
    # Syscall test: getchar -> putchar
    # This test expects one byte on stdin; if none, getchar may return EOF.
    ori a7, x0, 1    # syscall: getchar
    ecall
    # a0 should contain char read; pass to putchar
    ori a7, x0, 2    # syscall: putchar
    ecall
    ori a7, x0, 93   # exit
    ecall
