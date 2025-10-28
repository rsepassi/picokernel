// RISC-V Interrupt Handling
// Setup trap vector and interrupt control

#include "interrupt.h"
#include "printk.h"
#include <stdint.h>

// External trap handler (defined in trap.S)
extern void trap_vector(void);

// Forward declare timer handler
void timer_handler(void);

// CSR access macros
#define read_csr(reg)                                                          \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    __asm__ volatile("csrr %0, " #reg : "=r"(__tmp));                          \
    __tmp;                                                                     \
  })

#define write_csr(reg, val)                                                    \
  ({ __asm__ volatile("csrw " #reg ", %0" ::"rK"(val)); })

#define set_csr(reg, bit)                                                      \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    __asm__ volatile("csrrs %0, " #reg ", %1" : "=r"(__tmp) : "rK"(bit));      \
    __tmp;                                                                     \
  })

#define clear_csr(reg, bit)                                                    \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    __asm__ volatile("csrrc %0, " #reg ", %1" : "=r"(__tmp) : "rK"(bit));      \
    __tmp;                                                                     \
  })

// sstatus bits
#define SSTATUS_SIE (1UL << 1) // Supervisor Interrupt Enable

// sie/sip bits
#define SIE_STIE (1UL << 5) // Supervisor Timer Interrupt Enable

// scause interrupt bit
#define SCAUSE_INTERRUPT (1UL << 63)

// Exception codes (non-interrupt)
static const char *exception_names[] = {"Instruction address misaligned",
                                        "Instruction access fault",
                                        "Illegal instruction",
                                        "Breakpoint",
                                        "Load address misaligned",
                                        "Load access fault",
                                        "Store/AMO address misaligned",
                                        "Store/AMO access fault",
                                        "Environment call from U-mode",
                                        "Environment call from S-mode",
                                        "Reserved",
                                        "Environment call from M-mode",
                                        "Instruction page fault",
                                        "Load page fault",
                                        "Reserved",
                                        "Store/AMO page fault"};

// Trap handler called from assembly (trap.S)
void trap_handler(uint64_t scause, uint64_t sepc, uint64_t stval) {
  if (scause & SCAUSE_INTERRUPT) {
    // Interrupt
    uint64_t int_code = scause & ~SCAUSE_INTERRUPT;
    if (int_code == 5) {
      // Supervisor timer interrupt
      timer_handler();
    } else {
      printk("Unhandled interrupt: ");
      printk_hex64(int_code);
      printk("\n");
    }
  } else {
    // Exception
    printk("\n!!! EXCEPTION: ");
    if (scause < sizeof(exception_names) / sizeof(exception_names[0])) {
      printk(exception_names[scause]);
    } else {
      printk("Unknown");
    }
    printk(" (code ");
    printk_hex64(scause);
    printk(") !!!\n");
    printk("sepc: ");
    printk_hex64(sepc);
    printk("\n");
    printk("stval: ");
    printk_hex64(stval);
    printk("\n");
    printk("System halted.\n");

    // Halt the CPU
    while (1) {
      __asm__ volatile("wfi");
    }
  }
}

// Initialize trap handling
void interrupt_init(void) {
  // Set trap vector to direct mode (stvec = trap_vector)
  // Lower 2 bits = 00 for direct mode (all traps go to same handler)
  uint64_t tvec = (uint64_t)trap_vector;
  write_csr(stvec, tvec);

  // Enable supervisor timer interrupts
  set_csr(sie, SIE_STIE);

  printk("Trap handler initialized (stvec = ");
  printk_hex64(tvec);
  printk(")\n");
}

// Enable interrupts globally
void interrupt_enable(void) { set_csr(sstatus, SSTATUS_SIE); }

// Disable interrupts globally
void interrupt_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }
