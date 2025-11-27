#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "disassemble.h"
#include "memory.h"
#include "read_elf.h"

// Minimal stub disassembler: returns a simple textual representation
// This is a placeholder so the project links while you develop the
// full disassembler. It prints the instruction hex; hop targets and
// symbol resolution are not implemented here.
void disassemble(uint32_t addr, uint32_t instruction, char* result, size_t buf_size, struct symbols* symbols) {
	(void) addr;
	(void) symbols;
	if (result == NULL || buf_size == 0) return;
	// Print a concise placeholder: mnemonic and raw hex
	snprintf(result, buf_size, "unimpl\t0x%08x", instruction);
}



