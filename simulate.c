#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <stdbool.h>
#include <string.h>

#include "simulate.h"
#include "memory.h"
#include "disassemble.h" // så vi kan logge pænt med tekst
#include "read_elf.h"    // symbols er deklareret her

/* Debug: define to print every fetched instruction (helps diagnose opcode issues) */
/* #define DEBUG_FETCH 1 */

// --- Branch Prediction Configuration ---
#define NUM_SIZES 4
static const int predictor_sizes[NUM_SIZES] = {256, 1024, 4096, 16384};

// --- Hjælpefunktioner ---------------------------------------------

// Sign-extend 'value' som har 'bits' betydende bits (f.eks. 12 for I-immediates)
static int32_t sign_extend(uint32_t value, int bits)
{
    uint32_t mask = 1u << (bits - 1); // fortegnbit
    if (value & mask)
    {
        value |= ~((1u << bits) - 1);
    }
    return (int32_t)value;
}

// I-type: imm[11:0] = instr[31:20]
static int32_t imm_i(uint32_t instr)
{
    uint32_t imm = instr >> 20;
    return sign_extend(imm, 12);
}

// S-type: imm[11:5] = instr[31:25], imm[4:0] = instr[11:7]
static int32_t imm_s(uint32_t instr)
{
    uint32_t imm = 0;
    imm |= (instr >> 25) & 0x7F; // 11:5
    imm <<= 5;
    imm |= (instr >> 7) & 0x1F; // 4:0
    return sign_extend(imm, 12);
}

// B-type: imm[12|10:5|4:1|11] = instr[31|30:25|11:8|7]
static int32_t imm_b(uint32_t instr)
{
    uint32_t imm = 0;
    imm |= ((instr >> 31) & 0x1) << 12;
    imm |= ((instr >> 7) & 0x1) << 11;
    imm |= ((instr >> 25) & 0x3F) << 5;
    imm |= ((instr >> 8) & 0xF) << 1;
    return sign_extend(imm, 13);
}

// U-type: imm[31:12] = instr[31:12]
static int32_t imm_u(uint32_t instr)
{
    uint32_t imm = instr & 0xFFFFF000u;
    return (int32_t)imm;
}

// J-type: imm[20|10:1|11|19:12] = instr[31|30:21|20|19:12]
static int32_t imm_j(uint32_t instr)
{
    uint32_t imm = 0;
    imm |= ((instr >> 31) & 0x1) << 20;
    imm |= ((instr >> 21) & 0x3FF) << 1;
    imm |= ((instr >> 20) & 0x1) << 11;
    imm |= ((instr >> 12) & 0xFF) << 12;
    return sign_extend(imm, 21);
}

// Multiply high helpers using only 32-bit arithmetic.
// Return high 32 bits of unsigned 32x32->64 multiplication.
static uint32_t mulhu_u32(uint32_t a, uint32_t b)
{
    uint32_t a0 = a & 0xFFFFu;
    uint32_t a1 = a >> 16;
    uint32_t b0 = b & 0xFFFFu;
    uint32_t b1 = b >> 16;

    uint32_t z0 = a0 * b0; // fits in 32 bits
    uint32_t z1 = a0 * b1;
    uint32_t z2 = a1 * b0;
    uint32_t z3 = a1 * b1;

    uint32_t sum_z1z2 = z1 + z2;
    uint32_t carry_z1z2 = (sum_z1z2 < z1) ? 1u : 0u; // carry out of 32 bits

    // high = z3 + (sum_z1z2 >> 16) + (carry_z1z2 << 16)
    uint32_t high = z3 + (sum_z1z2 >> 16) + (carry_z1z2 << 16);

    // carry from lower 32 bits: (z0 + (sum_z1z2 << 16)) overflow?
    uint32_t lower_add = (sum_z1z2 << 16);
    uint32_t carry_lower = ((uint32_t)(z0 + lower_add) < z0) ? 1u : 0u;

    return high + carry_lower;
}

// Return high 32 bits of signed 32x32->64 multiplication.
static uint32_t mulh_s32(int32_t a_signed, int32_t b_signed)
{
    // Compute absolute values as uint32_t (works for INT32_MIN)
    uint32_t ua = (a_signed < 0) ? (~(uint32_t)a_signed) + 1u : (uint32_t)a_signed;
    uint32_t ub = (b_signed < 0) ? (~(uint32_t)b_signed) + 1u : (uint32_t)b_signed;

    uint32_t high = mulhu_u32(ua, ub);
    uint32_t low = ua * ub;

    // If signs differ, negate 64-bit (high:low)
    if ((a_signed < 0) ^ (b_signed < 0))
    {
        low = ~low + 1u;                      // two's complement low
        high = ~high + (low == 0u ? 1u : 0u); // propagate carry
    }
    return high;
}

// Return high 32 bits of signed(a) * unsigned(b)
static uint32_t mulhsu_s32(int32_t a_signed, uint32_t b_unsigned)
{
    uint32_t ua = (a_signed < 0) ? (~(uint32_t)a_signed) + 1u : (uint32_t)a_signed;

    uint32_t high = mulhu_u32(ua, b_unsigned);
    uint32_t low = ua * b_unsigned;

    if (a_signed < 0)
    {
        low = ~low + 1u;
        high = ~high + (low == 0u ? 1u : 0u);
    }
    return high;
}

// --- Selve simulatoren ---------------------------------------------

struct Stat simulate(struct memory *mem, int start_addr, FILE *log_file, struct symbols *symbols)
{
    struct Stat stat = {0};

    // 32 general-purpose registre
    uint32_t x[32] = {0};
    uint32_t pc = (uint32_t)start_addr;

    int running = 1;
    bool next_is_jump_target = false; // mark the next instruction if it's a jump/branch target

    // --- Branch Prediction Setup ---
    // Simple predictors
    long int nt_total = 0, nt_miss = 0;
    long int btfnt_total = 0, btfnt_miss = 0;

    // Bimodal: array of 2-bit saturating counters for each size
    uint8_t *bimodal[NUM_SIZES];
    long int bimodal_total[NUM_SIZES] = {0};
    long int bimodal_miss[NUM_SIZES] = {0};

    // gShare: array of 2-bit saturating counters + global history register
    uint8_t *gshare[NUM_SIZES];
    long int gshare_total[NUM_SIZES] = {0};
    long int gshare_miss[NUM_SIZES] = {0};
    uint32_t ghr = 0; // Global History Register (12 bits)

    // Allocate and initialize predictor tables
    for (int i = 0; i < NUM_SIZES; i++)
    {
        bimodal[i] = (uint8_t *)calloc(predictor_sizes[i], sizeof(uint8_t));
        gshare[i] = (uint8_t *)calloc(predictor_sizes[i], sizeof(uint8_t));
        // Initialize to weakly not-taken (state 1: 01 in binary)
        for (int j = 0; j < predictor_sizes[i]; j++)
        {
            bimodal[i][j] = 1;
            gshare[i][j] = 1;
        }
    }

    while (running)
    {
        // Hent instruktion fra lager (word)
        uint32_t instr = (uint32_t)memory_rd_w(mem, (int)pc);
        // Debug: print raw fetched instruction for tracing opcode issues
#ifdef DEBUG_FETCH
        fprintf(stderr, "DEBUG FETCH: PC=0x%08x INSTR=0x%08x\n", pc, instr);
#endif
        stat.insns++;

        // Prepare logging helpers for this instruction
        char asm_text[128] = {0};
        char reg_change[128] = "";
        char mem_change[128] = "";
        const char *branch_tag = "";
        bool was_jump_target = next_is_jump_target;
        next_is_jump_target = false;

        // Dekodér felter
        uint32_t opcode = instr & 0x7F;
        uint32_t rd = (instr >> 7) & 0x1F;
        uint32_t funct3 = (instr >> 12) & 0x7;
        uint32_t rs1 = (instr >> 15) & 0x1F;
        uint32_t rs2 = (instr >> 20) & 0x1F;
        uint32_t funct7 = (instr >> 25) & 0x7F;

        // Som udgangspunkt går vi videre til næste instruktion
        uint32_t next_pc = pc + 4;
        // Remember old destination register value to detect writes
        uint32_t old_rd_val = x[rd];
        switch (opcode)
        {

        // ---------------- R-type: OP (add, sub, and, or, mul, div, rem, ...)
        case 0x33:
        {
            switch (funct3)
            {
            case 0x0: // add / sub / mul
                if (funct7 == 0x00)
                { // add
                    x[rd] = x[rs1] + x[rs2];
                }
                else if (funct7 == 0x20)
                { // sub
                    x[rd] = x[rs1] - x[rs2];
                }
                else if (funct7 == 0x01)
                { // mul (M-extension)
                    x[rd] = (uint32_t)((int32_t)x[rs1] * (int32_t)x[rs2]);
                }
                break;
            case 0x4: // xor / div (signed)
                if (funct7 == 0x00)
                { // xor
                    x[rd] = x[rs1] ^ x[rs2];
                }
                else if (funct7 == 0x01)
                { // div (M-extension, signed)
                    int32_t a = (int32_t)x[rs1];
                    int32_t b = (int32_t)x[rs2];
                    if (b == 0)
                    {
                        x[rd] = 0xFFFFFFFFu; // div-by-zero convention
                    }
                    else if (a == INT32_MIN && b == -1)
                    {
                        x[rd] = (uint32_t)INT32_MIN; // overflow case
                    }
                    else
                    {
                        x[rd] = (uint32_t)(a / b);
                    }
                }
                break;

            case 0x6: // or / rem (signed)
                if (funct7 == 0x00)
                {
                    x[rd] = x[rs1] | x[rs2]; // or
                }
                else if (funct7 == 0x01)
                {
                    int32_t a = (int32_t)x[rs1];
                    int32_t b = (int32_t)x[rs2];
                    if (b == 0)
                    {
                        x[rd] = (uint32_t)a; // rem-by-zero convention
                    }
                    else if (a == INT32_MIN && b == -1)
                    {
                        x[rd] = 0; // defined result for overflow case
                    }
                    else
                    {
                        x[rd] = (uint32_t)(a % b);
                    }
                }
                break;

            case 0x7: // and / remu (unsigned remainder if M-extension)
                if (funct7 == 0x00)
                {
                    x[rd] = x[rs1] & x[rs2];
                }
                else if (funct7 == 0x01)
                {
                    uint32_t a = x[rs1];
                    uint32_t b = x[rs2];
                    if (b == 0)
                        x[rd] = a;
                    else
                        x[rd] = a % b;
                }
                break;

            case 0x1: // sll / mulh
                if (funct7 == 0x00)
                {
                    x[rd] = x[rs1] << (x[rs2] & 0x1F);
                }
                else if (funct7 == 0x01)
                {
                    // mulh: signed * signed -> high 32 bits (32-bit-only helper)
                    x[rd] = mulh_s32((int32_t)x[rs1], (int32_t)x[rs2]);
                }
                break;

            case 0x5: // srl / sra / divu
                if (funct7 == 0x00)
                { // srl
                    x[rd] = x[rs1] >> (x[rs2] & 0x1F);
                }
                else if (funct7 == 0x20)
                { // sra
                    x[rd] = (uint32_t)((int32_t)x[rs1] >> (x[rs2] & 0x1F));
                }
                else if (funct7 == 0x01)
                { // divu (M-extension unsigned)
                    uint32_t a = x[rs1];
                    uint32_t b = x[rs2];
                    if (b == 0)
                        x[rd] = 0xFFFFFFFFu;
                    else
                        x[rd] = a / b;
                }
                break;

            case 0x2: // slt / mulhsu
                if (funct7 == 0x01)
                {
                    // mulhsu: signed * unsigned -> high 32 bits (32-bit-only helper)
                    x[rd] = mulhsu_s32((int32_t)x[rs1], (uint32_t)x[rs2]);
                }
                else
                {
                    x[rd] = ((int32_t)x[rs1] < (int32_t)x[rs2]) ? 1u : 0u;
                }
                break;

            case 0x3: // sltu / mulhu
                if (funct7 == 0x01)
                {
                    // mulhu: unsigned * unsigned -> high 32 bits (32-bit-only helper)
                    x[rd] = mulhu_u32((uint32_t)x[rs1], (uint32_t)x[rs2]);
                }
                else
                {
                    x[rd] = (x[rs1] < x[rs2]) ? 1u : 0u;
                }
                break;

            default:
                // ukendt R-instruktion
                fprintf(stderr, "Unknown R-type at 0x%08x\n", pc);
                running = 0;
                break;
            }
            break;
        }

        // ---------------- I-type: OP-IMM (addi, andi, ori, xori, slli, srli, srai, slti, sltiu)
        case 0x13:
        {
            int32_t imm = imm_i(instr);
            switch (funct3)
            {
            case 0x0: // addi
                x[rd] = x[rs1] + imm;
                break;
            case 0x7: // andi
                x[rd] = x[rs1] & (uint32_t)imm;
                break;
            case 0x6: // ori
                x[rd] = x[rs1] | (uint32_t)imm;
                break;
            case 0x4: // xori
                x[rd] = x[rs1] ^ (uint32_t)imm;
                break;
            case 0x2: // slti
                x[rd] = ((int32_t)x[rs1] < imm) ? 1u : 0u;
                break;
            case 0x3: // sltiu
                x[rd] = (x[rs1] < (uint32_t)imm) ? 1u : 0u;
                break;
            case 0x1:
            { // slli
                uint32_t shamt = (instr >> 20) & 0x1F;
                x[rd] = x[rs1] << shamt;
                break;
            }
            case 0x5:
            { // srli / srai
                uint32_t shamt = (instr >> 20) & 0x1F;
                if ((instr >> 30) & 0x1)
                { // bit 30 = 1 => srai
                    x[rd] = (uint32_t)((int32_t)x[rs1] >> shamt);
                }
                else
                {
                    x[rd] = x[rs1] >> shamt;
                }
                break;
            }
            default:
                fprintf(stderr, "Unknown OP-IMM at 0x%08x\n", pc);
                running = 0;
                break;
            }
            break;
        }

        // ---------------- Loads: opcode 0x03
        case 0x03:
        {
            int32_t imm = imm_i(instr);
            int addr = (int)((int32_t)x[rs1] + imm);
            switch (funct3)
            {
            case 0x0:
            { // lb
                int8_t v = (int8_t)memory_rd_b(mem, addr);
                x[rd] = (int32_t)v;
                break;
            }
            case 0x1:
            { // lh
                int16_t v = (int16_t)memory_rd_h(mem, addr);
                x[rd] = (int32_t)v;
                break;
            }
            case 0x2: // lw
                x[rd] = (uint32_t)memory_rd_w(mem, addr);
                break;
            case 0x4: // lbu
                x[rd] = (uint32_t)memory_rd_b(mem, addr);
                break;
            case 0x5: // lhu
                x[rd] = (uint32_t)memory_rd_h(mem, addr);
                break;
            default:
                fprintf(stderr, "Unknown load at 0x%08x\n", pc);
                running = 0;
                break;
            }
            break;
        }

        // ---------------- Stores: opcode 0x23
        case 0x23:
        {
            int32_t imm = imm_s(instr);
            int addr = (int)((int32_t)x[rs1] + imm);
            switch (funct3)
            {
            case 0x0: // sb
                memory_wr_b(mem, addr, (int)(x[rs2] & 0xFF));
                if (log_file != NULL)
                {
                    snprintf(mem_change, sizeof(mem_change), "M[%08x] <- %02x", addr, (unsigned)(x[rs2] & 0xFF));
                }
                break;
            case 0x1: // sh
                memory_wr_h(mem, addr, (int)(x[rs2] & 0xFFFF));
                if (log_file != NULL)
                {
                    snprintf(mem_change, sizeof(mem_change), "M[%08x] <- %04x", addr, (unsigned)(x[rs2] & 0xFFFF));
                }
                break;
            case 0x2: // sw
                memory_wr_w(mem, addr, (int)x[rs2]);
                if (log_file != NULL)
                {
                    snprintf(mem_change, sizeof(mem_change), "M[%08x] <- %08x", addr, x[rs2]);
                }
                break;
            default:
                fprintf(stderr, "Unknown store at 0x%08x\n", pc);
                running = 0;
                break;
            }
            break;
        }

        // ---------------- Branches: opcode 0x63
        case 0x63:
        {
            int32_t imm = imm_b(instr);
            int32_t pc_signed = (int32_t)pc;
            int32_t target = pc_signed + imm;

            switch (funct3)
            {
            case 0x0: // beq
                if (x[rs1] == x[rs2])
                    next_pc = (uint32_t)target;
                break;
            case 0x1: // bne
                if (x[rs1] != x[rs2])
                    next_pc = (uint32_t)target;
                break;
            case 0x4: // blt
                if ((int32_t)x[rs1] < (int32_t)x[rs2])
                    next_pc = (uint32_t)target;
                break;
            case 0x5: // bge
                if ((int32_t)x[rs1] >= (int32_t)x[rs2])
                    next_pc = (uint32_t)target;
                break;
            case 0x6: // bltu
                if (x[rs1] < x[rs2])
                    next_pc = (uint32_t)target;
                break;
            case 0x7: // bgeu
                if (x[rs1] >= x[rs2])
                    next_pc = (uint32_t)target;
                break;
            default:
                fprintf(stderr, "Unknown branch at 0x%08x\n", pc);
                running = 0;
                break;
            }

            // --- Branch Prediction Logic ---
            // Determine if branch was actually taken
            int taken = (next_pc != pc + 4);

            // NT (Not Taken): Always predict not taken
            nt_total++;
            int nt_pred = 0;
            if (nt_pred != taken)
                nt_miss++;

            // BTFNT (Backward Taken, Forward Not Taken)
            btfnt_total++;
            int backward = (imm < 0);
            int btfnt_pred = backward ? 1 : 0;
            if (btfnt_pred != taken)
                btfnt_miss++;

            // Bimodal and gShare predictors
            for (int i = 0; i < NUM_SIZES; i++)
            {
                int size = predictor_sizes[i];

                // --- Bimodal Predictor ---
                // Index using lower bits of PC (word-aligned, so shift by 2)
                int bimodal_idx = (pc >> 2) % size;
                bimodal_total[i]++;
                // 2-bit counter: >= 2 predicts taken, < 2 predicts not taken
                int bimodal_pred = (bimodal[i][bimodal_idx] >= 2) ? 1 : 0;
                if (bimodal_pred != taken)
                    bimodal_miss[i]++;
                // Update counter: increment if taken (saturate at 3), decrement if not taken (saturate at 0)
                if (taken)
                {
                    if (bimodal[i][bimodal_idx] < 3)
                        bimodal[i][bimodal_idx]++;
                }
                else
                {
                    if (bimodal[i][bimodal_idx] > 0)
                        bimodal[i][bimodal_idx]--;
                }

                // --- gShare Predictor ---
                // Index using XOR of PC bits and global history
                // Use appropriate number of bits for the table size
                int ghr_bits = 0;
                int temp_size = size;
                while (temp_size > 1)
                {
                    ghr_bits++;
                    temp_size >>= 1;
                }
                uint32_t ghr_mask = (1u << ghr_bits) - 1;
                int gshare_idx = ((pc >> 2) ^ (ghr & ghr_mask)) % size;
                gshare_total[i]++;
                int gshare_pred = (gshare[i][gshare_idx] >= 2) ? 1 : 0;
                if (gshare_pred != taken)
                    gshare_miss[i]++;
                // Update counter
                if (taken)
                {
                    if (gshare[i][gshare_idx] < 3)
                        gshare[i][gshare_idx]++;
                }
                else
                {
                    if (gshare[i][gshare_idx] > 0)
                        gshare[i][gshare_idx]--;
                }
            }

            // Update global history register (shift left, insert taken bit)
            ghr = ((ghr << 1) | taken) & 0xFFF; // Keep 12 bits

            break;
        }

        // ---------------- LUI: opcode 0x37
        case 0x37:
        {
            int32_t imm = imm_u(instr);
            x[rd] = (uint32_t)imm;
            break;
        }

        // ---------------- AUIPC: opcode 0x17
        case 0x17:
        {
            int32_t imm = imm_u(instr);
            x[rd] = pc + imm;
            break;
        }

        // ---------------- JAL: opcode 0x6F
        case 0x6F:
        {
            int32_t imm = imm_j(instr);
            uint32_t ret_addr = pc + 4;
            x[rd] = ret_addr;
            int32_t target = (int32_t)pc + imm;
            next_pc = (uint32_t)target;
            next_is_jump_target = true;
            break;
        }

        // ---------------- JALR: opcode 0x67
        case 0x67:
        {
            int32_t imm = imm_i(instr);
            uint32_t ret_addr = pc + 4;
            uint32_t target = (x[rs1] + imm) & ~1u;
            x[rd] = ret_addr;
            next_pc = target;
            next_is_jump_target = true;
            break;
        }

        // ---------------- SYSTEM: opcode 0x73 (ecall, ebreak, ...)
        case 0x73:
        {
            if (instr == 0x00000073)
            {
                // ECALL - a7 (x17) contains syscall number
                uint32_t call = x[17];
                switch (call)
                {
                case 1:
                { // inp: return char in a0 (x10)
                    int c = getchar();
                    if (c == EOF)
                        x[10] = (uint32_t)-1;
                    else
                        x[10] = (uint32_t)c;
                    break;
                }
                case 2:
                { // outp: write char from a0 (x10)
                    int c = (int)(x[10] & 0xFF);
                    putchar(c);
                    fflush(stdout);
                    x[10] = 0; // return value (unused)
                    break;
                }
                case 3:
                { // terminate: exit with code in a0
                    running = 0;
                    break;
                }
                case 93:
                { // exit (alternative exit number per spec)
                    running = 0;
                    break;
                }
                case 4:
                { // read_int_buffer(file, buffer, max_size)
                    // Minimal stub: do nothing, return 0
                    x[10] = 0;
                    break;
                }
                case 5:
                { // write_int_buffer(file, buffer, size)
                    // Minimal stub: pretend all written, return size (a2)
                    x[10] = x[12];
                    break;
                }
                case 6:
                { // open_file / close_file - not supported
                    x[10] = (uint32_t)-1;
                    break;
                }
                default:
                {
                    // Unknown syscall: stop execution
                    running = 0;
                    break;
                }
                }
            }
            else if (instr == 0x00100073)
            {
                // EBREAK – stop execution
                running = 0;
            }
            else
            {
                fprintf(stderr, "Unknown SYSTEM instr at 0x%08x\n", pc);
                running = 0;
            }
            break;
        }

        default:
            fprintf(stderr, "Unknown opcode 0x%02x at 0x%08x\n", opcode, pc);
            running = 0;
            break;
        }

        // Detect register write to rd (any instruction that writes rd will be caught)
        if (rd != 0 && x[rd] != old_rd_val)
        {
            snprintf(reg_change, sizeof(reg_change), "R[%2u] <- %08x", rd, x[rd]);
        }

        // For conditional branches we already set branch_tag when taken.

        // If there was a memory write, handlers have filled `mem_change` already.

        // If logging is enabled, produce a single-line entry with details.
        if (log_file != NULL)
        {
            // Disassemble text for this instruction
            disassemble(pc, instr, asm_text, sizeof(asm_text), symbols);

            // Prefix: indicate if this instruction was the target of a jump
            const char *prefix = was_jump_target ? "=>" : "  ";

            // Compose extras: branch_tag, reg_change, mem_change
            char extras[256] = "";
            if (branch_tag[0])
                snprintf(extras + strlen(extras), sizeof(extras) - strlen(extras), " %s", branch_tag);
            if (reg_change[0])
                snprintf(extras + strlen(extras), sizeof(extras) - strlen(extras), " %s", reg_change);
            if (mem_change[0])
                snprintf(extras + strlen(extras), sizeof(extras) - strlen(extras), " %s", mem_change);

            fprintf(log_file, "%6ld %2s  %08x : %08x    %-30s %s\n",
                    stat.insns, prefix, pc, instr, asm_text, extras);
        }

        // x0 er altid 0 i RISC-V
        x[0] = 0;

        // Opdatér PC til næste instruktion (eller branch/jump-target)
        pc = next_pc;
    }

    // --- Report Branch Prediction Results ---
    printf("\n=== Branch Prediction Results ===\n");
    printf("\n[NT] Not Taken:\n");
    printf("  Predictions: %ld\n", nt_total);
    printf("  Mispredictions: %ld\n", nt_miss);
    if (nt_total > 0)
    {
        printf("  Accuracy: %.2f%%\n", 100.0 * (nt_total - nt_miss) / nt_total);
    }

    printf("\n[BTFNT] Backward Taken, Forward Not Taken:\n");
    printf("  Predictions: %ld\n", btfnt_total);
    printf("  Mispredictions: %ld\n", btfnt_miss);
    if (btfnt_total > 0)
    {
        printf("  Accuracy: %.2f%%\n", 100.0 * (btfnt_total - btfnt_miss) / btfnt_total);
    }

    printf("\n[Bimodal] Dynamic 2-bit Predictor:\n");
    for (int i = 0; i < NUM_SIZES; i++)
    {
        printf("  Size %d:\n", predictor_sizes[i]);
        printf("    Predictions: %ld\n", bimodal_total[i]);
        printf("    Mispredictions: %ld\n", bimodal_miss[i]);
        if (bimodal_total[i] > 0)
        {
            printf("    Accuracy: %.2f%%\n", 100.0 * (bimodal_total[i] - bimodal_miss[i]) / bimodal_total[i]);
        }
    }

    printf("\n[gShare] Global History Predictor:\n");
    for (int i = 0; i < NUM_SIZES; i++)
    {
        printf("  Size %d:\n", predictor_sizes[i]);
        printf("    Predictions: %ld\n", gshare_total[i]);
        printf("    Mispredictions: %ld\n", gshare_miss[i]);
        if (gshare_total[i] > 0)
        {
            printf("    Accuracy: %.2f%%\n", 100.0 * (gshare_total[i] - gshare_miss[i]) / gshare_total[i]);
        }
    }
    printf("\n");

    // Free predictor tables
    for (int i = 0; i < NUM_SIZES; i++)
    {
        free(bimodal[i]);
        free(gshare[i]);
    }

    return stat;
}