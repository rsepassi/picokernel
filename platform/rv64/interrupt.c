// RISC-V Interrupt Handling
// Setup trap vector and interrupt control

#include "interrupt.h"
#include "platform.h"
#include "printk.h"
#include <stddef.h>
#include <stdint.h>

// External trap handler (defined in trap.S)
extern void trap_vector(void);

// Forward declare timer handler (from timer.c)
void timer_handler(platform_t *platform);

// Forward declare trap_handler (called from trap.S)
void trap_handler(uint64_t scause, uint64_t sepc, uint64_t stval);

// Module-local platform pointer
// ARCHITECTURAL LIMITATION: trap_handler() is called from assembly (trap.S)
// and cannot receive parameters. This is the same limitation as ARM64's
// exception_handler() - assembly exception vectors cannot pass context.
// This pointer is set once during interrupt_init() and used for IRQ dispatch.
static platform_t *g_current_platform = NULL;

// PLIC (Platform-Level Interrupt Controller) base addresses for QEMU virt
// RISC-V PLIC has separate contexts for M-mode and S-mode
// Context 0 = M-mode hart 0, Context 1 = S-mode hart 0
// Since we run in S-mode, we must use context 1 registers
#define PLIC_BASE 0x0C000000ULL
#define PLIC_PRIORITY_BASE (PLIC_BASE + 0x000000)
#define PLIC_PENDING_BASE (PLIC_BASE + 0x001000)
#define PLIC_ENABLE_BASE (PLIC_BASE + 0x002080)   // Context 1 (S-mode hart 0)
#define PLIC_THRESHOLD_BASE (PLIC_BASE + 0x201000) // Context 1 (S-mode hart 0)
#define PLIC_CLAIM_BASE (PLIC_BASE + 0x201004)     // Context 1 (S-mode hart 0)

// Memory-mapped register access helpers
static inline void mmio_write32(uint64_t addr, uint32_t value) {
  platform_mmio_write32((volatile uint32_t *)addr, value);
}

static inline uint32_t mmio_read32(uint64_t addr) {
  return platform_mmio_read32((volatile uint32_t *)addr);
}

// CSR access macros
#define read_csr(reg)                                                          \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    __asm__ volatile("csrr %0, " #reg : "=r"(__tmp));                          \
    __tmp;                                                                     \
  })

#define write_csr(reg, val)                                                    \
  do {                                                                         \
    __asm__ volatile("csrw " #reg ", %0" ::"rK"(val));                         \
  } while (0)

#define set_csr(reg, bit)                                                      \
  do {                                                                         \
    __asm__ volatile("csrs " #reg ", %0" ::"rK"(bit));                         \
  } while (0)

#define clear_csr(reg, bit)                                                    \
  do {                                                                         \
    __asm__ volatile("csrc " #reg ", %0" ::"rK"(bit));                         \
  } while (0)

// sstatus bits
#define SSTATUS_SIE (1UL << 1) // Supervisor Interrupt Enable

// sie/sip bits
#define SIE_STIE (1UL << 5) // Supervisor Timer Interrupt Enable
#define SIE_SEIE (1UL << 9) // Supervisor External Interrupt Enable

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
      timer_handler(g_current_platform);
    } else if (int_code == 9) {
      // Supervisor external interrupt (from PLIC)
      // Claim the interrupt from PLIC
      uint32_t irq = mmio_read32(PLIC_CLAIM_BASE);

      if (irq > 0 && irq < MAX_IRQS) {
        // Dispatch to registered handler
        irq_dispatch(irq);
      }

      // Complete the interrupt by writing back the IRQ number
      // PLIC will re-trigger if more interrupts are pending
      if (irq > 0) {
        mmio_write32(PLIC_CLAIM_BASE, irq);
      }
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

// Initialize PLIC (Platform-Level Interrupt Controller)
static void plic_init(platform_t *platform) {
  // Set all interrupt priorities to 1 (non-zero to enable)
  for (uint32_t i = 1; i < MAX_IRQS; i++) {
    mmio_write32(PLIC_PRIORITY_BASE + (i * 4), 1);
  }

  // Set priority threshold to 0 (accept all priorities)
  mmio_write32(PLIC_THRESHOLD_BASE, 0);

  // Initialize IRQ table
  for (uint32_t i = 0; i < MAX_IRQS; i++) {
    platform->irq_table[i].handler = NULL;
    platform->irq_table[i].context = NULL;
  }

  printk("PLIC initialized at 0x");
  printk_hex64(PLIC_BASE);
  printk("\n");
}

// Initialize trap handling
void interrupt_init(platform_t *platform) {
  // Store platform pointer for trap_handler()
  g_current_platform = platform;

  // Set trap vector to direct mode (stvec = trap_vector)
  // Lower 2 bits = 00 for direct mode (all traps go to same handler)
  uint64_t tvec = (uint64_t)trap_vector;
  write_csr(stvec, tvec);

  // Enable supervisor timer and external interrupts
  set_csr(sie, SIE_STIE | SIE_SEIE);

  printk("Trap handler initialized (stvec = ");
  printk_hex64(tvec);
  printk(")\n");

  // Initialize PLIC
  plic_init(platform);
}

// Enable interrupts globally
void platform_interrupt_enable(platform_t *platform) {
  (void)platform; // Unused on RISC-V
  set_csr(sstatus, SSTATUS_SIE);
}

// Disable interrupts globally
void platform_interrupt_disable(platform_t *platform) {
  (void)platform; // Unused on RISC-V
  clear_csr(sstatus, SSTATUS_SIE);
}

// Register IRQ handler
void irq_register(platform_t *platform, uint32_t irq_num,
                  void (*handler)(void *), void *context) {
  if (!platform || irq_num >= MAX_IRQS) {
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
  if (!platform || irq_num >= MAX_IRQS) {
    return;
  }

  // Enable interrupt in PLIC
  // Each 32-bit register controls 32 interrupts (1 bit per interrupt)
  uint32_t reg_offset = (irq_num / 32) * 4;
  uint32_t bit = irq_num % 32;
  uint32_t val = mmio_read32(PLIC_ENABLE_BASE + reg_offset);
  val |= (1 << bit);
  mmio_write32(PLIC_ENABLE_BASE + reg_offset, val);

  printk("IRQ ");
  printk_dec(irq_num);
  printk(" enabled in PLIC\n");
}

// Dispatch IRQ to registered handler
void irq_dispatch(uint32_t irq_num) {
  if (!g_current_platform || irq_num >= MAX_IRQS) {
    return;
  }

  if (g_current_platform->irq_table[irq_num].handler != NULL) {
    g_current_platform->irq_table[irq_num].handler(
        g_current_platform->irq_table[irq_num].context);
  }
}
