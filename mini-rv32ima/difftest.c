// Difftest wrapper for mini-rv32ima
// This file provides a Spike-compatible difftest API for NEMU

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Define macros for mini-rv32ima
#define MINIRV32WARN(x...)
#define MINIRV32_DECORATE static
#define MINI_RV32_RAM_SIZE (0x8000000)
#define MINIRV32_IMPLEMENTATION
#define MINIRV32_POSTEXEC(pc, ir, retval)

#include "mini-rv32ima.h"

// Export symbols with default visibility
#define __EXPORT __attribute__((visibility("default")))

// Difftest directions
#define DIFFTEST_TO_DUT 0
#define DIFFTEST_TO_REF 1

// Difftest context structure (matches NEMU's CPU state)
struct diff_context_t {
    uint32_t prv;           // Privilege level
    uint32_t gpr[32];       // General purpose registers
    uint32_t pc;            // Program counter

    uint32_t mepc;
    uint32_t mcause;
    uint32_t mtvec;
    uint32_t mstatus;

    uint32_t mie;
    uint32_t mscratch;
    uint32_t mtval;
    uint32_t mip;

    uint32_t wdata;         // Write data for difftest_store
};

// Global state
static struct MiniRV32IMAState *mini_state = NULL;
static uint8_t *mini_ram = NULL;

// Initialize the mini-rv32ima difftest reference
__EXPORT void difftest_init(int port) {
    // Allocate RAM
    mini_ram = (uint8_t*)malloc(MINI_RV32_RAM_SIZE);
    if (!mini_ram) {
        fprintf(stderr, "Failed to allocate mini-rv32ima RAM\n");
        exit(1);
    }
    memset(mini_ram, 0, MINI_RV32_RAM_SIZE);

    // Allocate state at the end of RAM (like the original implementation)
    mini_state = (struct MiniRV32IMAState*)(mini_ram + MINI_RV32_RAM_SIZE - sizeof(struct MiniRV32IMAState));
    memset(mini_state, 0, sizeof(struct MiniRV32IMAState));

    // Initialize PC to reset vector
    mini_state->pc = MINIRV32_RAM_IMAGE_OFFSET;
    mini_state->extraflags = 3;  // Machine mode

    printf("[mini-rv32ima difftest] Initialized with %dMB RAM\n", MINI_RV32_RAM_SIZE / (1024*1024));
}

// Copy memory between DUT and REF
__EXPORT void difftest_memcpy(uint32_t addr, void *buf, size_t n, bool direction) {
    if (direction == DIFFTEST_TO_REF) {
        // Copy from DUT to mini-rv32ima
        uint32_t offset = addr - MINIRV32_RAM_IMAGE_OFFSET;
        if (offset + n <= MINI_RV32_RAM_SIZE) {
            memcpy(mini_ram + offset, buf, n);
        } else {
            fprintf(stderr, "[mini-rv32ima] Memory copy out of bounds: addr=0x%x, size=%zu\n", addr, n);
        }
    } else {
        // DIFFTEST_TO_DUT - not typically used
        fprintf(stderr, "[mini-rv32ima] difftest_memcpy TO_DUT not implemented\n");
    }
}

// Copy registers between DUT and REF
__EXPORT void difftest_regcpy(void* dut, bool direction) {
    struct diff_context_t* ctx = (struct diff_context_t*)dut;

    if (direction == DIFFTEST_TO_REF) {
        // Copy from DUT to mini-rv32ima
        for (int i = 0; i < 32; i++) {
            mini_state->regs[i] = ctx->gpr[i];
        }
        mini_state->pc = ctx->pc;
        mini_state->mepc = ctx->mepc;
        mini_state->mcause = ctx->mcause;
        mini_state->mtvec = ctx->mtvec;
        mini_state->mstatus = ctx->mstatus;
        mini_state->mie = ctx->mie;
        mini_state->mscratch = ctx->mscratch;
        mini_state->mtval = ctx->mtval;
        mini_state->mip = ctx->mip;

        // Extract privilege level from extraflags
        mini_state->extraflags = (mini_state->extraflags & ~3) | (ctx->prv & 3);
    } else {
        // Copy from mini-rv32ima to DUT
        for (int i = 0; i < 32; i++) {
            ctx->gpr[i] = mini_state->regs[i];
        }
        ctx->pc = mini_state->pc;
        ctx->mepc = mini_state->mepc;
        ctx->mcause = mini_state->mcause;
        ctx->mtvec = mini_state->mtvec;
        ctx->mstatus = mini_state->mstatus;
        ctx->mie = mini_state->mie;
        ctx->mscratch = mini_state->mscratch;
        ctx->mtval = mini_state->mtval;
        ctx->mip = mini_state->mip;

        // Extract privilege level from extraflags
        ctx->prv = mini_state->extraflags & 3;
    }
}

// Execute n instructions
__EXPORT void difftest_exec(uint64_t n) {
    // Execute n instructions
    // MiniRV32IMAStep returns 0 on success
    int ret = MiniRV32IMAStep(mini_state, mini_ram, 0, 0, n);
    if (ret != 0) {
        fprintf(stderr, "[mini-rv32ima] Execution failed with code %d\n", ret);
    }
}

// Raise an interrupt
__EXPORT void difftest_raise_intr(uint64_t NO) {
    // Simulate an interrupt by setting mcause and jumping to trap handler
    uint32_t pc = mini_state->pc;

    // Set mcause
    mini_state->mcause = NO;
    mini_state->mtval = 0;

    // Save current PC to mepc
    mini_state->mepc = pc;

    // Save and update mstatus
    // Move MIE to MPIE, clear MIE
    mini_state->mstatus = ((mini_state->mstatus & 0x08) << 4) |
                          ((mini_state->extraflags & 3) << 11);

    // Jump to trap handler
    mini_state->pc = mini_state->mtvec;

    // Enter machine mode
    mini_state->extraflags |= 3;
}

// Get data from memory after a store operation
__EXPORT void difftest_store(uint32_t waddr, uint32_t *wdata) {
    // Read the data from mini-rv32ima's memory
    uint32_t offset = waddr - MINIRV32_RAM_IMAGE_OFFSET;
    if (offset + 4 <= MINI_RV32_RAM_SIZE) {
        *wdata = *(uint32_t*)(mini_ram + offset);
    } else {
        fprintf(stderr, "[mini-rv32ima] Store read out of bounds: addr=0x%x\n", waddr);
        *wdata = 0;
    }
}

__EXPORT void difftest_mem(uint32_t addr) {
    uint32_t offset = addr - MINIRV32_RAM_IMAGE_OFFSET;
    if (offset + 4 <= MINI_RV32_RAM_SIZE) {
				printf("(ref) addr = 0x%08x, offset = 0x%08x, mem = 0x%08x\n", addr, offset, *(uint32_t *)(mini_ram + offset));
    } else {
        fprintf(stderr, "[mini-rv32ima] difftest mem addr=0x%x out of bounds[0x%08x : 0x%08x]\n", addr, MINIRV32_RAM_IMAGE_OFFSET, MINIRV32_RAM_IMAGE_OFFSET + MINI_RV32_RAM_SIZE);
    }
}
