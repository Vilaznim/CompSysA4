#!/usr/bin/env bash
# Build and run the provided RISC-V test programs using ./gcc.py and ./sim
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
GCCPY="$ROOT/gcc.py"

OUTDIR="$HERE/build"
mkdir -p "$OUTDIR"

tests=( r_tests i_tests mem_branch_tests m_tests syscall_tests )

# Find a local cross-compiler if available
CC_LOCAL=""
for c in riscv32-unknown-elf-gcc riscv64-unknown-elf-gcc; do
  if command -v "$c" >/dev/null 2>&1; then
    CC_LOCAL=$(command -v "$c")
    break
  fi
done

for t in "${tests[@]}"; do
  src="$HERE/${t}.s"
  out="$OUTDIR/${t}.elf"
  echo "Compiling $src -> $out"

  if [ -n "$CC_LOCAL" ]; then
    echo "Using local cross-compiler: $CC_LOCAL"
    # Use flags to produce RV32 ELF; rely on -march/-mabi to select 32-bit
    "$CC_LOCAL" -march=rv32im -mabi=ilp32 -O0 -nostartfiles -nostdlib -Ttext=0x0 "$src" -o "$out" || true
  else
    if [ -x "$GCCPY" ]; then
      python3 "$GCCPY" -O0 -o "$out" "$src" || true
    else
      echo "No local cross-compiler and $GCCPY not executable; cannot compile $src"
      continue
    fi
  fi

  # Quick sanity check: ensure the produced file is an ELF
  if [ -f "$out" ]; then
    file "$out" | grep -i elf >/dev/null 2>&1 || {
      echo "Compilation of $src failed or produced non-ELF output. Removing $out"
      rm -f "$out"
      continue
    }
  else
    echo "Output $out missing; skipping run."
    continue
  fi

  echo "Running $out (log to $OUTDIR/${t}.log)"
  # Run sim with summary logging (-s) to capture final stats
  "$ROOT/sim" "$out" -s "$OUTDIR/${t}.log" || true
done

echo "All tests compiled and run (where possible). Logs in $OUTDIR"
