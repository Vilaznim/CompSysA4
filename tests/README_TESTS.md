# Tests for the RISC-V simulator/disassembler

This directory contains small assembly test programs and a helper script to compile them using `./gcc.py` (a helper script that uploads source to the course cross-compiler service) and run them under `./sim`.

Files:
- `r_tests.s` — R-type instruction tests
- `i_tests.s` — I-type instruction tests
- `mem_branch_tests.s` — memory and branch tests
- `m_tests.s` — M-extension (mul/div/rem) tests
- `syscall_tests.s` — system call (ecall) tests
- `build_and_run_tests.sh` — script to compile and run the tests

How to run:

1. Build the simulator (if not already built):

```bash
make
```

2. Run the tests (this will use `./gcc.py` to produce `.elf` files and then run `./sim`):

```bash
cd tests
./build_and_run_tests.sh
```

What to provide me after running:

- The contents of `tests/build/*.log` (or one representative log per test) so I can include instruction traces in the report.
- The printed branch-prediction summary (the simulator prints this to stdout) for the benchmark(s) you ran.
- Any observed mismatches (e.g., expected register values vs simulator output).

If you do not have network access or prefer an offline toolchain, you can instead build `.elf` files with your local cross-compiler (e.g. `riscv32-unknown-elf-gcc`) and place them into `tests/build/` before running `./sim`.
