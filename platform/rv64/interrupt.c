// RISC-V Interrupt Handling
// Setup trap vector and interrupt control

#include "interrupt.h"
#include "printk.h"
#include <stddef.h>
#include <stdint.h>

// External trap handler (defined in trap.S)
extern void trap_vector(void);

// Forward declare timer handler
void timer_handler(void);

// Forward declare trap_handler (called from trap.S)
void trap_handler(uint64_t scause, uint64_t sepc, uint64_t stval);

// PLIC (Platform-Level Interrupt Controller) base addresses for QEMU virt
#define PLIC_BASE 0x0C000000ULL
#define PLIC_PRIORITY_BASE (PLIC_BASE + 0x000000)
#define PLIC_PENDING_BASE (PLIC_BASE + 0x001000)
#define PLIC_ENABLE_BASE (PLIC_BASE + 0x002000)
#define PLIC_THRESHOLD_BASE (PLIC_BASE + 0x200000)
#define PLIC_CLAIM_BASE (PLIC_BASE + 0x200004)

// Maximum number of external interrupts in QEMU virt
#define MAX_IRQS 128

// Memory-mapped register access helpers
static inline void mmio_write32(uint64_t addr, uint32_t value) {
  *(volatile uint32_t *)addr = value;
}

static inline uint32_t mmio_read32(uint64_t addr) {
  return *(volatile uint32_t *)addr;
}

// IRQ handler table entry
typedef struct {
  void *context;
  void (*handler)(void *context);
} irq_entry_t;

// IRQ dispatch table
static irq_entry_t g_irq_table[MAX_IRQS];

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
      timer_handler();
    } else if (int_code == 9) {
      // Supervisor external interrupt (from PLIC)
      // Claim the interrupt from PLIC
      uint32_t irq = mmio_read32(PLIC_CLAIM_BASE);

      if (irq > 0 && irq < MAX_IRQS) {
        // Dispatch to registered handler
        irq_dispatch(irq);
      }

      // Complete the interrupt by writing back the IRQ number
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
static void plic_init(void) {
  // Set all interrupt priorities to 1 (non-zero to enable)
  for (uint32_t i = 1; i < MAX_IRQS; i++) {
    mmio_write32(PLIC_PRIORITY_BASE + (i * 4), 1);
  }

  // Set priority threshold to 0 (accept all priorities)
  mmio_write32(PLIC_THRESHOLD_BASE, 0);

  printk("PLIC initialized at 0x");
  printk_hex64(PLIC_BASE);
  printk("\n");
}

// Initialize trap handling
void interrupt_init(void) {
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
  plic_init();
}

// Enable interrupts globally
void platform_interrupt_enable(void) { set_csr(sstatus, SSTATUS_SIE); }

// Disable interrupts globally
void platform_interrupt_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }

// Register IRQ handler
void irq_register(uint32_t irq_num, void (*handler)(void *), void *context) {
  if (irq_num >= MAX_IRQS) {
    return;
  }

  g_irq_table[irq_num].handler = handler;
  g_irq_table[irq_num].context = context;

  printk("IRQ ");
  printk_dec(irq_num);
  printk(" registered\n");
}

// Enable (unmask) a specific IRQ in the PLIC
void irq_enable(uint32_t irq_num) {
  if (irq_num >= MAX_IRQS) {
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
  if (irq_num >= MAX_IRQS) {
    return;
  }

  if (g_irq_table[irq_num].handler != NULL) {
    g_irq_table[irq_num].handler(g_irq_table[irq_num].context);
  }
}
