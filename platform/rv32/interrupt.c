// RISC-V Interrupt Handling
// Trap vector setup and interrupt dispatching

#include "interrupt.h"
#include "platform.h"
#include "platform_impl.h"
#include "printk.h"
#include "timer.h"
#include <stddef.h>

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

// PLIC (Platform-Level Interrupt Controller) base address for QEMU virt
// RISC-V PLIC has separate contexts for M-mode and S-mode
// Context 0 = M-mode hart 0, Context 1 = S-mode hart 0
// Since we run in S-mode, we must use context 1 registers
#define PLIC_BASE 0x0c000000UL
#define PLIC_PRIORITY_BASE (PLIC_BASE + 0x000000)
#define PLIC_PENDING_BASE (PLIC_BASE + 0x001000)
#define PLIC_ENABLE_BASE (PLIC_BASE + 0x002080)   // Context 1 (S-mode hart 0)
#define PLIC_THRESHOLD (PLIC_BASE + 0x201000)     // Context 1 (S-mode hart 0)
#define PLIC_CLAIM (PLIC_BASE + 0x201004)         // Context 1 (S-mode hart 0)

// Maximum number of IRQs supported by PLIC
#define MAX_IRQS 128

// CSR access macros
#define read_csr(reg)                                                          \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    __asm__ volatile("csrr %0, " #reg : "=r"(__tmp));                          \
    __tmp;                                                                     \
  })

#define write_csr(reg, val)                                                    \
  ({ __asm__ volatile("csrw " #reg ", %0" ::"r"(val)); })

#define set_csr(reg, bit)                                                      \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    __asm__ volatile("csrrs %0, " #reg ", %1" : "=r"(__tmp) : "r"(bit));       \
    __tmp;                                                                     \
  })

#define clear_csr(reg, bit)                                                    \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    __asm__ volatile("csrrc %0, " #reg ", %1" : "=r"(__tmp) : "r"(bit));       \
    __tmp;                                                                     \
  })

// External trap handler entry point (defined in trap.S)
extern void trap_entry(void);

// MMIO helper functions
static inline void mmio_write32(uint32_t addr, uint32_t value) {
  platform_mmio_write32((volatile uint32_t *)addr, value);
}

static inline uint32_t mmio_read32(uint32_t addr) {
  return platform_mmio_read32((volatile uint32_t *)addr);
}

// Module-local platform pointer
// ARCHITECTURAL LIMITATION: trap_handler() is called from assembly (trap.S)
// and cannot receive parameters. This pointer is the only way to access
// platform state from the exception handler. This is acceptable as it's
// limited to interrupt dispatch and exception handling.
static platform_t *g_current_platform = NULL;

// Exception names for debugging (read-only lookup table)
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

// Initialize PLIC (Platform-Level Interrupt Controller)
static void plic_init(void) {
  printk("Initializing PLIC...\n");

  // Set priority for all interrupts to 1 (minimum non-zero priority)
  for (uint32_t i = 1; i < MAX_IRQS; i++) {
    mmio_write32(PLIC_PRIORITY_BASE + i * 4, 1);
  }

  // Set priority threshold to 0 (accept all priorities)
  mmio_write32(PLIC_THRESHOLD, 0);

  printk("PLIC initialized\n");
}

// Initialize interrupt handling
void interrupt_init(platform_t *platform) {
  printk("Setting up RISC-V trap handler...\n");

  // Store platform pointer for trap handler access
  g_current_platform = platform;

  // Initialize IRQ table
  for (int i = 0; i < MAX_IRQS; i++) {
    platform->irq_table[i].handler = NULL;
    platform->irq_table[i].context = NULL;
  }

  // Set trap vector to our handler (direct mode)
  // In direct mode, all traps jump to the base address
  uintptr_t trap_addr = (uintptr_t)&trap_entry;
  write_csr(stvec, trap_addr); // MODE=0 (direct)

  printk("Trap vector set to ");
  printk_hex32((uint32_t)trap_addr);
  printk("\n");

  // Initialize PLIC
  plic_init();

  // Enable timer and external interrupts in SIE
  set_csr(sie, SIE_STIE | SIE_SEIE);

  printk("Supervisor timer and external interrupts enabled\n");
}

// Enable interrupts globally
void platform_interrupt_enable(platform_t *platform) {
  (void)platform; // Unused - global CSR operation
  set_csr(sstatus, SSTATUS_SIE);
}

// Disable interrupts globally
void platform_interrupt_disable(platform_t *platform) {
  (void)platform; // Unused - global CSR operation
  clear_csr(sstatus, SSTATUS_SIE);
}

// Register IRQ handler
void irq_register(platform_t *platform, uint32_t irq_num,
                  void (*handler)(void *), void *context) {
  if (irq_num >= MAX_IRQS) {
    return;
  }

  platform->irq_table[irq_num].handler = handler;
  platform->irq_table[irq_num].context = context;

  printk("IRQ ");
  printk_dec(irq_num);
  printk(" registered\n");
}

// Enable (unmask) a specific IRQ in the PLIC
void irq_enable(platform_t *platform, uint32_t irq_num) {
  (void)platform; // Unused - PLIC enable is global

  if (irq_num >= MAX_IRQS) {
    return;
  }

  // Enable interrupt in PLIC (each 32-bit word contains enables for 32 IRQs)
  uint32_t enable_reg = PLIC_ENABLE_BASE + (irq_num / 32) * 4;
  uint32_t enable_bit = 1 << (irq_num % 32);
  uint32_t current = mmio_read32(enable_reg);
  mmio_write32(enable_reg, current | enable_bit);

  printk("IRQ ");
  printk_dec(irq_num);
  printk(" enabled in PLIC\n");
}

// Dispatch IRQ to registered handler
void irq_dispatch(platform_t *platform, uint32_t irq_num) {
  if (irq_num >= MAX_IRQS) {
    return;
  }

  if (platform->irq_table[irq_num].handler != NULL) {
    platform->irq_table[irq_num].handler(platform->irq_table[irq_num].context);
  }
}

// Common trap handler (called from trap.S after saving context)
void trap_handler(void) {
  unsigned long scause = read_csr(scause);
  unsigned long sepc = read_csr(sepc);

  // Access platform via module-local pointer
  // This is necessary because trap_handler() is called from assembly
  platform_t *platform = g_current_platform;

  // Check if it's an interrupt or exception
  if (scause & CAUSE_INTERRUPT_BIT) {
    // Interrupt
    unsigned long cause_code = scause & ~CAUSE_INTERRUPT_BIT;

    switch (cause_code) {
    case CAUSE_TIMER_INTERRUPT:
      // Supervisor timer interrupt
      timer_interrupt_handler(platform);
      break;

    case CAUSE_SOFTWARE_INTERRUPT:
      printk("Software interrupt\n");
      break;

    case CAUSE_EXTERNAL_INTERRUPT:
      // External interrupt - claim IRQ from PLIC
      {
        uint32_t irq = mmio_read32(PLIC_CLAIM);
        if (irq > 0 && irq < MAX_IRQS) {
          // Dispatch to registered handler
          irq_dispatch(platform, irq);
        }

        // Complete the interrupt (write IRQ number back to PLIC)
        // PLIC will re-trigger if more interrupts are pending
        if (irq > 0) {
          mmio_write32(PLIC_CLAIM, irq);
        }
      }
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
      __asm__ volatile("wfi");
    }
  }
}
