// RISC-V Interrupt Handling
// Trap vector setup and interrupt dispatching

#include "interrupt.h"
#include "printk.h"
#include "timer.h"

// CSR register definitions (Supervisor mode)
#define CSR_SSTATUS 0x100
#define CSR_SIE 0x104
#define CSR_STVEC 0x105
#define CSR_SCAUSE 0x142
#define CSR_SEPC 0x141

// SIE bits
#define SIE_STIE (1 << 5) // Supervisor timer interrupt enable
#define SIE_SSIE (1 << 1) // Supervisor software interrupt enable
#define SIE_SEIE (1 << 9) // Supervisor external interrupt enable

// SSTATUS bits
#define SSTATUS_SIE (1 << 1) // Supervisor interrupt enable

// Exception/interrupt codes (Supervisor mode)
#define CAUSE_INTERRUPT_BIT (1UL << 31)
#define CAUSE_TIMER_INTERRUPT 5    // Supervisor timer interrupt
#define CAUSE_SOFTWARE_INTERRUPT 1 // Supervisor software interrupt
#define CAUSE_EXTERNAL_INTERRUPT 9 // Supervisor external interrupt

// CSR access macros
#define read_csr(reg)                                                          \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    asm volatile("csrr %0, " #reg : "=r"(__tmp));                              \
    __tmp;                                                                     \
  })

#define write_csr(reg, val) ({ asm volatile("csrw " #reg ", %0" ::"r"(val)); })

#define set_csr(reg, bit)                                                      \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    asm volatile("csrrs %0, " #reg ", %1" : "=r"(__tmp) : "r"(bit));           \
    __tmp;                                                                     \
  })

#define clear_csr(reg, bit)                                                    \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    asm volatile("csrrc %0, " #reg ", %1" : "=r"(__tmp) : "r"(bit));           \
    __tmp;                                                                     \
  })

// External trap handler entry point (defined in trap.S)
extern void trap_entry(void);

// Exception names for debugging
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

// Initialize interrupt handling
void interrupt_init(void) {
  printk("Setting up RISC-V trap handler...\n");

  // Set trap vector to our handler (direct mode)
  // In direct mode, all traps jump to the base address
  uintptr_t trap_addr = (uintptr_t)&trap_entry;
  write_csr(stvec, trap_addr); // MODE=0 (direct)

  printk("Trap vector set to ");
  printk_hex32((uint32_t)trap_addr);
  printk("\n");

  // Enable timer interrupts in SIE
  set_csr(sie, SIE_STIE);

  printk("Supervisor timer interrupt enabled\n");
}

// Enable interrupts globally
void platform_interrupt_enable(void) { set_csr(sstatus, SSTATUS_SIE); }

// Disable interrupts globally
void platform_interrupt_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }

// Common trap handler (called from trap.S after saving context)
void trap_handler(void) {
  unsigned long scause = read_csr(scause);
  unsigned long sepc = read_csr(sepc);

  // Check if it's an interrupt or exception
  if (scause & CAUSE_INTERRUPT_BIT) {
    // Interrupt
    unsigned long cause_code = scause & ~CAUSE_INTERRUPT_BIT;

    switch (cause_code) {
    case CAUSE_TIMER_INTERRUPT:
      // Supervisor timer interrupt
      timer_interrupt_handler();
      break;

    case CAUSE_SOFTWARE_INTERRUPT:
      printk("Software interrupt\n");
      break;

    case CAUSE_EXTERNAL_INTERRUPT:
      printk("External interrupt\n");
      break;

    default:
      printk("Unknown interrupt: ");
      printk_dec(cause_code);
      printk("\n");
      break;
    }
  } else {
    // Exception
    unsigned long cause_code = scause;

    printk("\n!!! EXCEPTION: ");
    if (cause_code < sizeof(exception_names) / sizeof(exception_names[0])) {
      printk(exception_names[cause_code]);
    } else {
      printk("Unknown");
    }
    printk(" (cause ");
    printk_dec(cause_code);
    printk(") !!!\n");

    printk("SEPC: ");
    printk_hex32(sepc);
    printk("\n");

    printk("System halted.\n");

    // Halt the CPU
    while (1) {
      asm volatile("wfi");
    }
  }
}
