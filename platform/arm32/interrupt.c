// ARM32 Interrupt Handling
// GICv2 (Generic Interrupt Controller) setup and interrupt handlers

#include "interrupt.h"
#include "printk.h"
#include <stdint.h>

// GICv2 register offsets (QEMU virt machine)
// Distributor base: 0x08000000
// CPU Interface base: 0x08010000

#define GICD_BASE 0x08000000UL
#define GICC_BASE 0x08010000UL

// Distributor registers
#define GICD_CTLR 0x000       // Distributor Control Register
#define GICD_TYPER 0x004      // Interrupt Controller Type Register
#define GICD_ISENABLER0 0x100 // Interrupt Set-Enable Registers
#define GICD_IPRIORITYR 0x400 // Interrupt Priority Registers
#define GICD_ITARGETSR 0x800  // Interrupt Processor Targets Registers
#define GICD_ICFGR 0xC00      // Interrupt Configuration Registers

// CPU Interface registers
#define GICC_CTLR 0x000 // CPU Interface Control Register
#define GICC_PMR 0x004  // Interrupt Priority Mask Register
#define GICC_IAR 0x00C  // Interrupt Acknowledge Register
#define GICC_EOIR 0x010 // End of Interrupt Register

// ARM Generic Timer virtual timer interrupt
// PPI 11 = IRQ 27 (16 + 11)
// Using virtual timer (CNTV) for ARMv7-A
#define TIMER_IRQ 27

// Memory-mapped register access helpers
static inline void gicd_write32(uint32_t offset, uint32_t value) {
  *(volatile uint32_t *)(GICD_BASE + offset) = value;
}

static inline uint32_t gicd_read32(uint32_t offset) {
  return *(volatile uint32_t *)(GICD_BASE + offset);
}

static inline void gicc_write32(uint32_t offset, uint32_t value) {
  *(volatile uint32_t *)(GICC_BASE + offset) = value;
}

static inline uint32_t gicc_read32(uint32_t offset) {
  return *(volatile uint32_t *)(GICC_BASE + offset);
}

// Maximum number of IRQs supported by GICv2
#define MAX_IRQS 1024

// IRQ handler table entry
typedef struct {
  void *context;
  void (*handler)(void *context);
} irq_entry_t;

// IRQ dispatch table
static irq_entry_t g_irq_table[MAX_IRQS];

// Forward declare timer handler
void timer_handler(void);

// Exception names for debugging
static const char *exception_names[8] = {"Reset",
                                         "Undefined Instruction",
                                         "Software Interrupt (SVC)",
                                         "Prefetch Abort",
                                         "Data Abort",
                                         "Reserved",
                                         "IRQ",
                                         "FIQ"};

// Common interrupt handler (called from assembly)
void interrupt_handler(uint32_t vector) {
  // Vector values:
  // 0 = Reset, 1 = Undefined, 2 = SVC, 3 = Prefetch Abort,
  // 4 = Data Abort, 5 = Reserved, 6 = IRQ, 7 = FIQ

  if (vector == 6) {
    // IRQ - read IAR to get interrupt ID
    uint32_t iar = gicc_read32(GICC_IAR);
    uint32_t irq = iar & 0x3FF; // Interrupt ID is in bits [9:0]

    if (irq == TIMER_IRQ) {
      // Timer interrupt
      timer_handler();
    } else if (irq >= 1020) {
      // Spurious interrupt (1020-1023 are special values)
      // No EOI needed for spurious interrupts
      return;
    } else {
      // Dispatch to registered handler
      irq_dispatch(irq);
    }

    // Send EOI (End of Interrupt)
    gicc_write32(GICC_EOIR, iar);
  } else {
    // Exception - print diagnostic and halt
    printk("\n!!! EXCEPTION: ");
    if (vector < 8) {
      printk(exception_names[vector]);
    } else {
      printk("Unknown");
    }
    printk(" (vector ");
    printk_dec(vector);
    printk(") !!!\n");
    printk("System halted.\n");

    // Halt the CPU
    while (1) {
      __asm__ volatile("wfi");
    }
  }
}

// Initialize GIC
static void gic_init(void) {
  // Disable distributor
  gicd_write32(GICD_CTLR, 0);

  // Read number of interrupt lines
  uint32_t typer = gicd_read32(GICD_TYPER);
  uint32_t num_lines = ((typer & 0x1F) + 1) * 32;

  printk("GICv2 initialized: ");
  printk_dec(num_lines);
  printk(" interrupt lines\n");

  // Set all interrupt priorities to a default value (0xA0)
  for (uint32_t i = 0; i < num_lines; i += 4) {
    gicd_write32(GICD_IPRIORITYR + i, 0xA0A0A0A0);
  }

  // Set all interrupts to target CPU 0
  // Each byte controls one interrupt, value is CPU mask (bit 0 = CPU 0)
  for (uint32_t i = 32; i < num_lines; i += 4) {
    gicd_write32(GICD_ITARGETSR + i, 0x01010101);
  }

  // Configure all interrupts as level-sensitive (default)
  for (uint32_t i = 0; i < num_lines / 16; i++) {
    gicd_write32(GICD_ICFGR + (i * 4), 0);
  }

  // Enable timer interrupt
  // ISENABLER registers: 32 interrupts per register
  uint32_t reg_idx = TIMER_IRQ / 32;
  uint32_t bit_idx = TIMER_IRQ % 32;
  gicd_write32(GICD_ISENABLER0 + (reg_idx * 4), 1 << bit_idx);

  printk("Enabled timer interrupt (IRQ ");
  printk_dec(TIMER_IRQ);
  printk(")\n");

  // Enable distributor (enable both Group 0 and Group 1 for non-secure mode)
  // Bit 0: Enable Group 0, Bit 1: Enable Group 1 (if in secure mode)
  // For non-secure mode, bit 0 enables non-secure interrupts
  gicd_write32(GICD_CTLR, 1);

  // Configure CPU interface
  // Set priority mask to allow all interrupts (0xFF = lowest priority)
  gicc_write32(GICC_PMR, 0xFF);

  // Enable CPU interface for IRQ and FIQ
  // Bit 0: Enable Group 0 interrupts
  // Try setting more enable bits to ensure interrupts are delivered
  gicc_write32(GICC_CTLR, 0x1);
}

// External assembly function to set VBAR (Vector Base Address Register)
extern void set_vbar(uint32_t addr);

// External vector table (defined in vectors.S)
extern uint32_t vectors_start;

// Initialize interrupts and exception vectors
void interrupt_init(void) {
  // Set vector table address
  set_vbar((uint32_t)&vectors_start);

  printk("Exception vectors installed at 0x");
  printk_hex32((uint32_t)&vectors_start);
  printk("\n");

  // Initialize GIC
  gic_init();
}

// Enable interrupts (clear I bit in CPSR)
void platform_interrupt_enable(void) {
  __asm__ volatile("mrs r0, cpsr\n"
                   "bic r0, r0, #0x80\n" // Clear I bit (bit 7)
                   "msr cpsr_c, r0\n" ::
                       : "r0");
}

// Disable interrupts (set I bit in CPSR)
void platform_interrupt_disable(void) {
  __asm__ volatile("mrs r0, cpsr\n"
                   "orr r0, r0, #0x80\n" // Set I bit (bit 7)
                   "msr cpsr_c, r0\n" ::
                       : "r0");
}

// Configure interrupt trigger type (level/edge)
// trigger: 0 = level-sensitive, 1 = edge-triggered
static void irq_set_trigger(uint32_t irq_num, uint32_t trigger) {
  if (irq_num >= MAX_IRQS) {
    return;
  }

  // GICD_ICFGR: 2 bits per interrupt
  // Bit [2n+1]: 0 = level-sensitive, 1 = edge-triggered
  // Bit [2n]: reserved (model-specific)
  uint32_t reg = irq_num / 16;
  uint32_t shift = (irq_num % 16) * 2;
  uint32_t val = gicd_read32(GICD_ICFGR + (reg * 4));

  // Clear the trigger bit
  val &= ~(0x2 << shift);

  // Set the trigger bit if edge-triggered
  if (trigger) {
    val |= (0x2 << shift);
  }

  gicd_write32(GICD_ICFGR + (reg * 4), val);
}

// Register IRQ handler
void irq_register(uint32_t irq_num, void (*handler)(void *), void *context) {
  if (irq_num >= MAX_IRQS) {
    return;
  }

  g_irq_table[irq_num].handler = handler;
  g_irq_table[irq_num].context = context;

  // VirtIO MMIO interrupts are edge-triggered (from device tree)
  // Configure as edge-triggered for all non-timer IRQs
  if (irq_num != TIMER_IRQ) {
    irq_set_trigger(irq_num, 1); // 1 = edge-triggered
  }

  printk("IRQ ");
  printk_dec(irq_num);
  printk(" registered (");
  printk(irq_num == TIMER_IRQ ? "level" : "edge");
  printk("-triggered, target CPU 0)\n");
}

// Enable (unmask) a specific IRQ in the GIC
void irq_enable(uint32_t irq_num) {
  if (irq_num >= MAX_IRQS) {
    return;
  }

  // Enable interrupt in GIC Distributor
  uint32_t reg_idx = irq_num / 32;
  uint32_t bit_idx = irq_num % 32;
  gicd_write32(GICD_ISENABLER0 + (reg_idx * 4), 1 << bit_idx);

  printk("IRQ ");
  printk_dec(irq_num);
  printk(" enabled in GIC\n");
}

// Dispatch IRQ to registered handler
void irq_dispatch(uint32_t irq_num) {
  if (irq_num >= MAX_IRQS) {
    return;
  }

  if (g_irq_table[irq_num].handler != NULL) {
    g_irq_table[irq_num].handler(g_irq_table[irq_num].context);
  } else {
    // Unknown interrupt - just print a warning
    printk("Unhandled IRQ: ");
    printk_dec(irq_num);
    printk("\n");
  }
}
