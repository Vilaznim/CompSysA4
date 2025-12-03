.globl _start
_start:
    # Syscall test: non-blocking putchar and exit
    # Use putchar to emit 'A' and then exit via syscall 3
    addi a0, x0, 65   # ASCII 'A'
    ori  a7, x0, 2    # syscall: putchar
    ecall
    ori  a7, x0, 3    # syscall: exit (3 also exits per spec)
    ecall
