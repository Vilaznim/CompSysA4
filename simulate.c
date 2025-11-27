#include <stdio.h>
#include "simulate.h"
#include "memory.h"
#include "read_elf.h"
#include <stdint.h>
#include <string.h>

// Minimal stub simulator: does not execute instructions. Returns zero
// instruction count. This stub lets you test argument parsing and the
// CLI in `main.c` while the full simulator is implemented.
struct Stat simulate(struct memory *mem, int start_addr, FILE *log_file, struct symbols* symbols) {
	(void) mem;
	(void) symbols;
	struct Stat s;
	s.insns = 0;
	if (log_file) {
		fprintf(log_file, "simulate: stub called with start_addr=0x%x\n", start_addr);
	}
	return s;
}





