#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>

#include "cpu.h"
#include "logging.h"
#include "gamepak.h"
#include "macros.h"
#include "constants.h"

/* Registers */
uint16_t PC = 0xC000; // Program counter
uint8_t SP = 0xFD;    // Stack pointer
uint8_t P = 0x24;     // Processor status
uint8_t A = 0;        // Accumulator
uint8_t X = 0;        // Index register X
uint8_t Y = 0;        // Index register Y

/* State */
uint32_t CYC = 0;     // Cycle counter
uint32_t SL = 241;    // Scanline -- todo

/* Memory */
gamepak_t gamepak;
uint8_t ram[RAM_SIZE];

uint8_t read(uint16_t idx) {
    if (idx > PRG_ROM_LOWER_LIMIT) {
        /* idx in PRG-ROM */

        // gamepak.prg_rom starts at 0 in code, but is indexed starting at 
        // 0x8000 in the memory map; so offset it.
        return gamepak.prg_rom[idx - PRG_ROM_LOWER_LIMIT];
    } else if (idx < RAM_MIRROR_UPPER_LIMIT) {
        /* idx in RAM or RAM mirror */
        return ram[idx % RAM_SIZE]; // modulus handles mirroring
    } else {
        ERROR("Couldn't fetch from memory");
    }
}

void write(uint16_t idx, uint8_t val) {
    if (idx < RAM_MIRROR_UPPER_LIMIT) {
        ram[idx % RAM_SIZE] = val;
    } else {
        ERROR("Couldn't write to memory");
    }
}

void stack_push(uint8_t val) {
    write(SP + 256, val);
    SP--;
}

uint8_t stack_pop() {
    SP++;
    return read(SP + 256);
}

void execute(uint8_t opcode) {
    uint8_t num_args = instr_bytes[opcode] - 1;
    uint8_t arg1 = read(PC + 1), arg2 = read(PC + 2);

    switch (num_args) {
        case 0:
            log_cpu_state(opcode, num_args);
            break;
        case 1:
            log_cpu_state(opcode, num_args, arg1);
            break;
        case 2:
            log_cpu_state(opcode, num_args, arg1, arg2);
            break;
        default:
            ERROR("Invalid number of arguments for instruction");
    }

    PC += instr_bytes[opcode];

    switch (opcode) {
        // JMP
        case 0x4C: { // absolute
            PC = COMBINE(arg1, arg2);
            break;
        }
        case 0x6C: { // indirect
            PC = read(COMBINE(arg1, arg2));
            break;
        }
        // LDX
        case 0xA2: { // immediate 
            X = arg1;
			SET_SIGN(X); // todo: possibly handling this wrong in some cases?
			SET_ZERO(X);
            break;
        }
        case 0xA6: { // zero-page absolute
            ERROR("Not yet implemented");
            break;
        }
        case 0xB6: { // zero-page indexed
            ERROR("Not yet implemented");
            break;
        }
        case 0xAE: { // absolute
            X = read(COMBINE(arg1, arg2));
            SET_SIGN(X);
            SET_ZERO(X);
            break;
        }
        case 0xBE: {
            // todo: https://wiki.nesdev.com/w/index.php/CPU_addressing_modes
            // I think this is `absolute indexed`
            ERROR("Not yet implemented");
            break;
        }
        // STX
        case 0x86: { // zero-page absolute
            write(COMBINE(0, arg1), X);
            break;
        }
        case 0x96: {
            ERROR("Not yet implemented");
            break;
        }
        case 0x8E: { // absolute
            write(COMBINE(arg1, arg2), X);
            break;
        }
        // JSR 
        case 0x20: {
            PC--;  // JSR pushes address - 1 onto the stack
            stack_push(UPPER(PC));
            stack_push(LOWER(PC));
            PC = COMBINE(arg1, arg2);
            break;
        }
        // NOP 
        case 0xEA: {
            break;
        }
        // SEC
        case 0x38: {
            SET_BIT(P, 0);
            break;
        }
        // BCS
        case 0xB0: {
            if (CARRY_SET()) {
                uint16_t rel = PC + arg1;
                BRANCH_CYCLE_INCREMENT(rel);
                PC = rel; 
            }
            break;
        }
        // BCC
        case 0x90: {
            if (!CARRY_SET()) {
                uint16_t rel = PC + arg1;
                BRANCH_CYCLE_INCREMENT(rel);
                PC = rel; 
            }
            break;
        }
        // CLC
        case 0x18: {
            CLR_BIT(P, 0);
            break;
        }
        // LDA
        case 0xA9: { // immediate 
            A = arg1;
			SET_SIGN(A);
			SET_ZERO(A);
            break;
        }
        case 0xAD: { // absolute 
            A = read(COMBINE(arg1, arg2));
            SET_SIGN(A);
            SET_ZERO(A);
            break; 
        }
        // BEQ
        case 0xF0: {
            if (ZERO_SET()) {
                uint16_t rel = PC + arg1;
                BRANCH_CYCLE_INCREMENT(rel);
                PC = rel; 
            }
            break;
        }
        // BNE
        case 0xD0: {
            if (!ZERO_SET()) {
                uint16_t rel = PC + arg1;
                BRANCH_CYCLE_INCREMENT(rel);
                PC = rel; 
            }
            break;
        }
        // STA
        case 0x85: { // zero-page absolute
            write(COMBINE(0, arg1), A);
            SET_SIGN(A);
            SET_ZERO(A);
            break;
        }
        // BIT
        case 0x24: { // zero-page absolute 
            uint8_t m = read(COMBINE(0, arg1));
            SET_SIGN(m);
            SET_ZERO((m & A));
            SET_OVERFLOW(m & 0x40); // copy bit 6
            break;
        }
        // BVS
        case 0x70: {
            if (OVERFLOW_SET()) {
                uint16_t rel = PC + arg1;
                BRANCH_CYCLE_INCREMENT(rel);
                PC = rel; 
            }
            break;
        }
        // BVC
        case 0x50: {
            if (!OVERFLOW_SET()) {
                uint16_t rel = PC + arg1;
                BRANCH_CYCLE_INCREMENT(rel);
                PC = rel; 
            }
            break;
        }
        // BPL
        case 0x10: {
            if (!SIGN_SET()) {
                uint16_t rel = PC + arg1;
                BRANCH_CYCLE_INCREMENT(rel);
                PC = rel; 
            }
            break;
        }
        // RTS
        case 0x60: {
            uint8_t pc_lower = stack_pop();
            uint8_t pc_upper = stack_pop();
            PC = COMBINE(pc_lower, pc_upper) + 1;
            break;
        }
        // SEI 
        case 0x78: {
            SET_INTERRUPT(1);
            break;
        }
        // SED
        case 0xF8: {
            SET_DECIMAL(1);
            break;
        }
        // PHP
        case 0x08: {
            // PHP and BRK push the flags with bit 4 true. 
            // IRQ and NMI push bit 4 false.
            SET_B(1);
            stack_push(P);
            SET_B(0);
            break;
        }
        // PLA
        case 0x68: {
            A = stack_pop();
            SET_SIGN(A);
            SET_ZERO(A);
            break;
        }
        // AND
        case 0x29: { // immediate 
            A &= arg1;
            SET_SIGN(A);
            SET_ZERO(A);
            break;
        }
        // CMP
        case 0xC9: { //immediate 
            uint8_t m = A - arg1;
            SET_SIGN(m);
            SET_ZERO(m);
            SET_CARRY(A >= arg1);
            break;
        }
        // CLD 
        case 0xD8: {
            SET_DECIMAL(0);
            break;
        }
        // PHA
        case 0x48: {
            stack_push(A);
            break;
        }
        // PLP
        case 0x28: {
            // PLP and RTI pull P from stack, but ignore bits 4 and 5
            uint8_t m = stack_pop();
            CPY_BIT(P, m, 4);
            CPY_BIT(P, m, 5);
            P = m;
            break;
        }
        // BMI
        case 0x30: {
            if (SIGN_SET()) {
                uint16_t rel = PC + arg1;
                BRANCH_CYCLE_INCREMENT(rel);
                PC = rel; 
            }
            break;
        }
        // ORA
        case 0x09: { // immediate 
            A |= arg1;
            SET_SIGN(A);
            SET_ZERO(A);
            break;
        }
        // CLV
        case 0xB8: {
            SET_OVERFLOW(0);
            break;
        }
        // EOR
        case 0x49: { // immediate 
            A ^= arg1;
            SET_SIGN(A);
            SET_ZERO(A);
            break;
        }
        // ADC
        case 0x69: { // immediate
            uint16_t sum = A + arg1 + CARRY_SET();
            SET_OVERFLOW(!((A ^ arg1) & 128) && ((A ^ sum) & 128));
            SET_CARRY(sum >= 256);
            A = (uint8_t) sum;
            SET_SIGN(A);
            SET_ZERO(A);
            break;
        }
        // SBC
        case 0xE9: { // immediate 
            uint16_t diff = A - arg1 - !CARRY_SET();
            SET_OVERFLOW(((A ^ arg1) & 128) && ((A ^ diff) & 128));
            SET_CARRY(diff < 256);
            A = (uint8_t) diff;
            SET_SIGN(A);
            SET_ZERO(A);
            break;
        }
        // LDY
        case 0xA0: { // immediate 
            Y = arg1;
			SET_SIGN(arg1);
			SET_ZERO(arg1);
            break;
        }
        // CPY
        case 0xC0: { // immediate 
            uint8_t m = Y - arg1;
            SET_SIGN(m);
            SET_ZERO(m);
            SET_CARRY(Y >= arg1);     
            break;
        }
        // CPX
        case 0xE0: { // immediate 
            uint8_t m = X - arg1;
            SET_SIGN(m);
            SET_ZERO(m);
            SET_CARRY(X >= arg1);
            break;
        }
        // INY 
        case 0xC8: {
            Y++;
            SET_SIGN(Y);
            SET_ZERO(Y);
            break;
        }
        // INX 
        case 0xE8: {
            X++;
            SET_SIGN(X);
            SET_ZERO(X);
            break;
        }
        // DEY
        case 0x88: {
            Y--;
            SET_SIGN(Y);
            SET_ZERO(Y);
            break;
        }
        // DEX 
        case 0xCA: {
            X--;
            SET_SIGN(X);
            SET_ZERO(X);
            break;
        }
        // TAY
        case 0xA8: {
            Y = A;
            SET_SIGN(Y);
            SET_ZERO(Y);
            break;
        }
        // TAX
        case 0xAA: {
            X = A;
            SET_SIGN(X);
            SET_ZERO(X);
            break;
        }
        // TYA
        case 0x98: {
            A = Y;
            SET_SIGN(A);
            SET_ZERO(A);
            break;
        }
        // TXA
        case 0x8A: {
            A = X;
            SET_SIGN(A);
            SET_ZERO(A);
            break;
        }
        // TSX
        case 0xBA: {
            X = SP;
            SET_SIGN(X);
            SET_ZERO(X);
            break;
        }
        // TXS
        case 0x9A: {
            SP = X;
            break;
        }
        // RTI
        case 0x40: {
            // PLP and RTI pull P from stack, but ignore bits 4 and 5
            uint8_t m = stack_pop();
            CPY_BIT(P, m, 4);
            CPY_BIT(P, m, 5);
            P = m;

            uint8_t pc_lower = stack_pop();
            uint8_t pc_upper = stack_pop();
            PC = COMBINE(pc_lower, pc_upper);
            break;
        }
        default:
            ERROR("Invalid opcode");
    }

    CYC += instr_ppu_cycles[opcode];
    if (CYC >= SL_RESET) { // todo: >= or > ?
        CYC = CYC % SL_RESET;
        SL++;
        if (SL > 260) SL = -1; 
    }
}

int run() {
    int res = 0;

    // todo: use ERROR for error handling in gamepak handler code?
    if ((res = load("test_files/nestest.nes", &gamepak)) != 0) {
        ERROR("Failed to load gamepak");
    }

    memset(ram, 0, sizeof(ram));

    begin_logging();

    while (true) {
        execute(read(PC));
    }

    end_logging();

    free(gamepak.trainer);
    free(gamepak.prg_rom);
    free(gamepak.chr_rom);

    return res;
}
