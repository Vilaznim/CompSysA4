.globl _start
_start:
  addi x1, x0, 5
  addi x2, x0, 0
  div  x3, x1, x2   # div by zero
  ori  a7, x0, 93
  ecall