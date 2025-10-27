// x64 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

// Halt the CPU until an interrupt fires
// This puts the processor in a low-power state instead of busy-waiting
// When an interrupt occurs, the CPU wakes up, handles the interrupt,
// and execution continues at the next instruction after hlt
static inline void cpu_halt(void)
{
    __asm__ volatile("hlt");
}
