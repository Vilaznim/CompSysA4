#include <stdio.h>
#include "disassemble.h"
#include "memory.h"
#include "read_elf.h"

static const char *reg_name(unsigned r) {
    static const char *names[32] = {
        "x0",  "x1",  "x2",  "x3",
        "x4",  "x5",  "x6",  "x7",
        "x8",  "x9",  "x10", "x11",
        "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x19",
        "x20", "x21", "x22", "x23",
        "x24", "x25", "x26", "x27",
        "x28", "x29", "x30", "x31"
    };
    return r < 32 ? names[r] : "x?";
}

static int32_t sign_extend(uint32_t value, int bits) {
    uint32_t mask = 1u << (bits - 1);        // bit for fortegn
    // hvis fortegnsbitten er sat, fyld resten med 1'ere
    if (value & mask) {
        value |= ~((1u << bits) - 1);
    }
    return (int32_t)value;
}

// Dekodering af de forskellige immediate-formater:

// I-type: imm[11:0] = instr[31:20]
static int32_t imm_i(uint32_t instr) {
    uint32_t imm = instr >> 20;
    return sign_extend(imm, 12);
}

// S-type: imm[11:5] = instr[31:25], imm[4:0] = instr[11:7]
static int32_t imm_s(uint32_t instr) {
    uint32_t imm = 0;
    imm |= (instr >> 25) & 0x7F;   // bits 11:5
    imm <<= 5;
    imm |= (instr >> 7) & 0x1F;    // bits 4:0
    return sign_extend(imm, 12);
}

// B-type: imm[12|10:5|4:1|11] = instr[31|30:25|11:8|7], mindste bit er 0
static int32_t imm_b(uint32_t instr) {
    uint32_t imm = 0;
    imm |= ((instr >> 31) & 0x1) << 12;  // bit 12
    imm |= ((instr >> 7)  & 0x1) << 11;  // bit 11
    imm |= ((instr >> 25) & 0x3F) << 5;  // bits 10:5
    imm |= ((instr >> 8)  & 0xF)  << 1;  // bits 4:1
    return sign_extend(imm, 13);
}

// U-type: imm[31:12] = instr[31:12], lower 12 bits er 0
static int32_t imm_u(uint32_t instr) {
    uint32_t imm = instr & 0xFFFFF000u;
    return (int32_t)imm;  // ingen sign-extend nødvendig, den bruges som 20-bit øverste
}

// J-type: imm[20|10:1|11|19:12] = instr[31|30:21|20|19:12], mindste bit 0
static int32_t imm_j(uint32_t instr) {
    uint32_t imm = 0;
    imm |= ((instr >> 31) & 0x1) << 20;   // bit 20
    imm |= ((instr >> 21) & 0x3FF) << 1;  // bits 10:1
    imm |= ((instr >> 20) & 0x1) << 11;   // bit 11
    imm |= ((instr >> 12) & 0xFF) << 12;  // bits 19:12
    return sign_extend(imm, 21);
}

void disassemble(uint32_t addr,
                 uint32_t instruction,
                 char *result,
                 size_t buf_size,
                 struct symbols *symbols)
{
    (void)symbols;  // Suppress unused parameter warning
    
    // Hvis bufferen er NULL eller størrelse 0, så gør ingenting
    if (result == NULL || buf_size == 0) {
        return;
    }

    // Dekodér basisfelter
    uint32_t opcode = instruction & 0x7F;
    uint32_t rd     = (instruction >> 7)  & 0x1F;
    uint32_t funct3 = (instruction >> 12) & 0x7;
    uint32_t rs1    = (instruction >> 15) & 0x1F;
    uint32_t rs2    = (instruction >> 20) & 0x1F;
    uint32_t funct7 = (instruction >> 25) & 0x7F;

    // Default: vis bare .word hvis vi ikke kender instruktionen
    // (god fallback så du kan se rå data)
    snprintf(result, buf_size, ".word 0x%08x", instruction);

    switch (opcode) {
        // R-type instruktioner (OP): add, sub, mul, div, ...
        case 0x33: {
            const char *mnemonic = NULL;

            if (funct3 == 0x0 && funct7 == 0x00) mnemonic = "add";
            else if (funct3 == 0x0 && funct7 == 0x20) mnemonic = "sub";
            else if (funct3 == 0x7 && funct7 == 0x00) mnemonic = "and";
            else if (funct3 == 0x6 && funct7 == 0x00) mnemonic = "or";
            else if (funct3 == 0x4 && funct7 == 0x00) mnemonic = "xor";
            else if (funct3 == 0x1 && funct7 == 0x00) mnemonic = "sll";
            else if (funct3 == 0x5 && funct7 == 0x00) mnemonic = "srl";
            else if (funct3 == 0x5 && funct7 == 0x20) mnemonic = "sra";
            else if (funct3 == 0x2 && funct7 == 0x00) mnemonic = "slt";
            else if (funct3 == 0x3 && funct7 == 0x00) mnemonic = "sltu";

            // M-extension: mul, mulh, mulhsu, mulhu, div, divu, rem, remu
            else if (funct3 == 0x0 && funct7 == 0x01) mnemonic = "mul";
            else if (funct3 == 0x1 && funct7 == 0x01) mnemonic = "mulh";
            else if (funct3 == 0x2 && funct7 == 0x01) mnemonic = "mulhsu";
            else if (funct3 == 0x3 && funct7 == 0x01) mnemonic = "mulhu";
            else if (funct3 == 0x4 && funct7 == 0x01) mnemonic = "div";
            else if (funct3 == 0x5 && funct7 == 0x01) mnemonic = "divu";
            else if (funct3 == 0x6 && funct7 == 0x01) mnemonic = "rem";
            else if (funct3 == 0x7 && funct7 == 0x01) mnemonic = "remu";

            if (mnemonic) {
                snprintf(result, buf_size, "%s %s, %s, %s",
                         mnemonic,
                         reg_name(rd),
                         reg_name(rs1),
                         reg_name(rs2));
            }
            break;
        }

        // I-type ALU (OP-IMM): addi, andi, ori, xori, slli, srli, srai
        case 0x13: {
            int32_t imm = imm_i(instruction);
            const char *mnemonic = NULL;

            if (funct3 == 0x0) mnemonic = "addi";
            else if (funct3 == 0x7) mnemonic = "andi";
            else if (funct3 == 0x6) mnemonic = "ori";
            else if (funct3 == 0x4) mnemonic = "xori";
            else if (funct3 == 0x1 && ((instruction >> 25) & 0x7F) == 0x00) mnemonic = "slli";
            else if (funct3 == 0x5 && ((instruction >> 25) & 0x7F) == 0x00) mnemonic = "srli";
            else if (funct3 == 0x5 && ((instruction >> 25) & 0x7F) == 0x20) mnemonic = "srai";

            if (mnemonic) {
                // shift-immediates er normalt 5-bit, men du kan bare printe imm.
                snprintf(result, buf_size, "%s %s, %s, %d",
                         mnemonic,
                         reg_name(rd),
                         reg_name(rs1),
                         imm);
            }
            break;
        }

        // Loads (I-type, opcode 0x03): lb, lh, lw, lbu, lhu
        case 0x03: {
            int32_t imm = imm_i(instruction);
            const char *mnemonic = NULL;

            if (funct3 == 0x0) mnemonic = "lb";
            else if (funct3 == 0x1) mnemonic = "lh";
            else if (funct3 == 0x2) mnemonic = "lw";
            else if (funct3 == 0x4) mnemonic = "lbu";
            else if (funct3 == 0x5) mnemonic = "lhu";

            if (mnemonic) {
                snprintf(result, buf_size, "%s %s, %d(%s)",
                         mnemonic,
                         reg_name(rd),
                         imm,
                         reg_name(rs1));
            }
            break;
        }

        // Stores (S-type, opcode 0x23): sb, sh, sw
        case 0x23: {
            int32_t imm = imm_s(instruction);
            const char *mnemonic = NULL;

            if (funct3 == 0x0) mnemonic = "sb";
            else if (funct3 == 0x1) mnemonic = "sh";
            else if (funct3 == 0x2) mnemonic = "sw";

            if (mnemonic) {
                snprintf(result, buf_size, "%s %s, %d(%s)",
                         mnemonic,
                         reg_name(rs2),
                         imm,
                         reg_name(rs1));
            }
            break;
        }

        // Branches (B-type, opcode 0x63): beq, bne, blt, bge, bltu, bgeu
        case 0x63: {
            int32_t imm = imm_b(instruction);
            const char *mnemonic = NULL;

            if (funct3 == 0x0) mnemonic = "beq";
            else if (funct3 == 0x1) mnemonic = "bne";
            else if (funct3 == 0x4) mnemonic = "blt";
            else if (funct3 == 0x5) mnemonic = "bge";
            else if (funct3 == 0x6) mnemonic = "bltu";
            else if (funct3 == 0x7) mnemonic = "bgeu";

            if (mnemonic) {
                uint32_t target = addr + imm;  // addr er adressen for denne instr.
                snprintf(result, buf_size, "%s %s, %s, 0x%08x",
                         mnemonic,
                         reg_name(rs1),
                         reg_name(rs2),
                         target);
            }
            break;
        }

        // LUI (U-type, opcode 0x37)
        case 0x37: {
            int32_t imm = imm_u(instruction);
            snprintf(result, buf_size, "lui %s, 0x%x",
                     reg_name(rd),
                     (unsigned)(imm >> 12));
            break;
        }

        // AUIPC (U-type, opcode 0x17)
        case 0x17: {
            int32_t imm = imm_u(instruction);
            snprintf(result, buf_size, "auipc %s, 0x%x",
                     reg_name(rd),
                     (unsigned)(imm >> 12));
            break;
        }

        // JAL (J-type, opcode 0x6F)
        case 0x6F: {
            int32_t imm = imm_j(instruction);
            uint32_t target = addr + imm;
            snprintf(result, buf_size, "jal %s, 0x%08x",
                     reg_name(rd),
                     target);
            break;
        }

        // JALR (I-type, opcode 0x67)
        case 0x67: {
            int32_t imm = imm_i(instruction);
            snprintf(result, buf_size, "jalr %s, %d(%s)",
                     reg_name(rd),
                     imm,
                     reg_name(rs1));
            break;
        }

        // SYSTEM (opcode 0x73): ecall, ebreak
        case 0x73: {
            // I-type layout, men funct3 er normalt 0
            if (instruction == 0x00000073) {
                snprintf(result, buf_size, "ecall");
            } else if (instruction == 0x00100073) {
                snprintf(result, buf_size, "ebreak");
            } else {
                snprintf(result, buf_size, "system 0x%08x", instruction);
            }
            break;
        }

        default:
            // Vi lod default være .word i starten af funktionen
            break;
    }
}

