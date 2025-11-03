// ARM32 Interrupt Handling
// GICv2 (Generic Interrupt Controller) setup and interrupt handlers

#include "interrupt.h"
#include "platform.h"
#include "platform_impl.h"
#include "printk.h"
#include "timer.h"
#include <stddef.h>
#include <stdint.h>

// GICv2 register offsets
#define GICD_CTLR_OFF 0x000       // Distributor Control Register
#define GICD_TYPER_OFF 0x004      // Interrupt Controller Type Register
#define GICD_ISENABLER_OFF 0x100 // Interrupt Set-Enable Registers
#define GICD_ICENABLER_OFF 0x180 // Interrupt Clear-Enable Registers
#define GICD_IPRIORITYR_OFF 0x400 // Interrupt Priority Registers
#define GICD_ITARGETSR_OFF 0x800  // Interrupt Processor Targets Registers
#define GICD_ICFGR_OFF 0xC00      // Interrupt Configuration Registers

// CPU Interface register offsets
#define GICC_CTLR_OFF 0x000 // CPU Interface Control Register
#define GICC_PMR_OFF 0x004  // Interrupt Priority Mask Register
#define GICC_IAR_OFF 0x00C  // Interrupt Acknowledge Register
#define GICC_EOIR_OFF 0x010 // End of Interrupt Register

// ARM Generic Timer virtual timer interrupt
// PPI 11 = IRQ 27 (16 + 11)
// Using virtual timer (CNTV) for ARMv7-A
#define TIMER_IRQ 27

// Maximum number of IRQs supported by GICv2
#define MAX_IRQS 1024

// Memory-mapped register access helpers
static inline void mmio_write32(uintptr_t addr, uint32_t value) {
  platform_mmio_write32((volatile uint32_t *)addr, value);
}

static inline uint32_t mmio_read32(uintptr_t addr) {
  return platform_mmio_read32((volatile uint32_t *)addr);
}

// Module-local platform pointer for interrupt_handler()
// This is needed because interrupt_handler() is called from assembly
// (vectors.S) and cannot receive parameters. This is an architectural
// limitation common to all platforms with exception vectors in assembly. Scope:
// Only used for interrupt dispatch, not a general-purpose global.
static platform_t *g_current_platform = NULL;

// Timer handler is declared in timer.h
void generic_timer_handler(void *context);

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
    if (g_current_platform == NULL || g_current_platform->gic_cpu_base == 0) {
      return; // GIC not initialized yet
    }
    uint32_t iar =
        mmio_read32(g_current_platform->gic_cpu_base + GICC_IAR_OFF);
    uint32_t irq = iar & 0x3FF; // Interrupt ID is in bits [9:0]

    if (irq >= 1020) {
      // Spurious interrupt (1020-1023 are special values)
      // No EOI needed for spurious interrupts
      return;
    }

    // Dispatch to registered handler (including timer)
    irq_dispatch(g_current_platform, irq);

    // Send EOI (End of Interrupt)
    // For level-triggered interrupts, use full IAR value per GICv2 spec
    mmio_write32(g_current_platform->gic_cpu_base + GICC_EOIR_OFF, iar);
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
static void gic_init(platform_t *platform) {
  uintptr_t gicd_base = platform->gic_dist_base;
  uintptr_t gicc_base = platform->gic_cpu_base;

  KASSERT(gicd_base != 0 && gicc_base != 0,
          "GIC addresses must be discovered from FDT");

  // Initialize GIC Distributor
  // Disable distributor during configuration
  mmio_write32(gicd_base + GICD_CTLR_OFF, 0);

  // Read number of interrupt lines
  uint32_t typer = mmio_read32(gicd_base + GICD_TYPER_OFF);
  uint32_t num_lines = ((typer & 0x1F) + 1) * 32;

  KDEBUG_LOG("GIC Distributor: %u interrupt lines", num_lines);

  // Disable all interrupts
  for (uint32_t i = 0; i < num_lines; i += 32) {
    mmio_write32(gicd_base + GICD_ICENABLER_OFF + (i / 32) * 4, 0xFFFFFFFF);
  }

  // Set priority for all interrupts to a default value
  for (uint32_t i = 0; i < num_lines; i += 4) {
    mmio_write32(gicd_base + GICD_IPRIORITYR_OFF + i, 0xA0A0A0A0);
  }

  // Set all SPIs (IRQs 32+) to target CPU 0
  KDEBUG_LOG("Setting SPI targets to CPU 0...");
  for (uint32_t i = 32; i < num_lines; i += 4) {
    mmio_write32(gicd_base + GICD_ITARGETSR_OFF + i, 0x01010101);
  }

  // Configure timer interrupt (IRQ 27)
  // Set target to CPU 0
  uint32_t target_reg = gicd_base + GICD_ITARGETSR_OFF + (TIMER_IRQ / 4) * 4;
  uint32_t target_shift = (TIMER_IRQ % 4) * 8;
  uint32_t target_val = mmio_read32(target_reg);
  target_val &= ~(0xFF << target_shift);
  target_val |= (0x01 << target_shift); // Target CPU 0
  mmio_write32(target_reg, target_val);

  // Enable timer interrupt
  mmio_write32(gicd_base + GICD_ISENABLER_OFF + (TIMER_IRQ / 32) * 4,
               1 << (TIMER_IRQ % 32));

  // Enable distributor
  mmio_write32(gicd_base + GICD_CTLR_OFF, 1);

  // Initialize GIC CPU Interface
  // Set priority mask to allow all priorities
  mmio_write32(gicc_base + GICC_PMR_OFF, 0xFF);

  // Enable CPU interface
  mmio_write32(gicc_base + GICC_CTLR_OFF, 1);

  KLOG("GIC initialized (Distributor at 0x%llx, CPU Interface at 0x%llx)",
       (unsigned long long)gicd_base, (unsigned long long)gicc_base);
}

// External assembly function to set VBAR (Vector Base Address Register)
extern void set_vbar(uint32_t addr);

// External vector table (defined in vectors.S)
extern uint32_t vectors_start;

// Initialize interrupts and exception vectors
void interrupt_init(platform_t *platform) {
  // Store platform pointer for interrupt handler
  g_current_platform = platform;

  // Set vector table address
  set_vbar((uint32_t)&vectors_start);

  KDEBUG_LOG("Exception vectors installed at 0x%08x",
             (unsigned int)&vectors_start);

  // Initialize GIC
  gic_init(platform);

  // Initialize IRQ ring
  kirq_ring_init(&platform->irq_ring);

  // Register timer interrupt with platform context
  irq_register(platform, TIMER_IRQ, generic_timer_handler, platform);
}

// Enable interrupts (clear I bit in CPSR)
void platform_interrupt_enable(platform_t *platform) {
  (void)platform; // Unused on ARM32
  __asm__ volatile("mrs r0, cpsr\n"
                   "bic r0, r0, #0x80\n" // Clear I bit (bit 7)
                   "msr cpsr_c, r0\n" ::
                       : "r0");
}

// Disable interrupts (set I bit in CPSR)
void platform_interrupt_disable(platform_t *platform) {
  (void)platform; // Unused on ARM32
  __asm__ volatile("mrs r0, cpsr\n"
                   "orr r0, r0, #0x80\n" // Set I bit (bit 7)
                   "msr cpsr_c, r0\n" ::
                       : "r0");
}

// Configure interrupt trigger type (level/edge)
// trigger: 0 = level-sensitive, 1 = edge-triggered
static void irq_set_trigger(platform_t *platform, uint32_t irq_num,
                            uint32_t trigger) {
  if (irq_num >= MAX_IRQS || platform->gic_dist_base == 0) {
    return;
  }

  // GICD_ICFGR: 2 bits per interrupt
  // Bit [2n+1]: 0 = level-sensitive, 1 = edge-triggered
  // Bit [2n]: reserved (model-specific)
  uint32_t reg = platform->gic_dist_base + GICD_ICFGR_OFF + (irq_num / 16) * 4;
  uint32_t shift = (irq_num % 16) * 2;
  uint32_t val = mmio_read32(reg);

  // Clear the trigger bit
  val &= ~(0x2 << shift);

  // Set the trigger bit if edge-triggered
  if (trigger) {
    val |= (0x2 << shift);
  }

  mmio_write32(reg, val);
}

// Register IRQ handler
void irq_register(platform_t *platform, uint32_t irq_num,
                  void (*handler)(void *), void *context) {
  if (irq_num >= MAX_IRQS) {
    return;
  }

  platform->irq_table[irq_num].handler = handler;
  platform->irq_table[irq_num].context = context;

  // VirtIO MMIO interrupts should be level-triggered on ARM32
  // This differs from ARM64 but matches QEMU's ARM32 virt machine behavior
  if (irq_num != TIMER_IRQ) {
    irq_set_trigger(platform, irq_num, 0); // 0 = level-sensitive
  }

  KDEBUG_LOG("IRQ %u registered (%s-triggered, target CPU 0)", irq_num,
             irq_num == TIMER_IRQ ? "level" : "level");
}

// Enable (unmask) a specific IRQ in the GIC
void irq_enable(platform_t *platform, uint32_t irq_num) {
  if (irq_num >= MAX_IRQS || platform->gic_dist_base == 0) {
    return;
  }

  // Enable interrupt in GIC Distributor
  mmio_write32(platform->gic_dist_base + GICD_ISENABLER_OFF +
                   (irq_num / 32) * 4,
               1 << (irq_num % 32));

  KDEBUG_LOG("IRQ %u enabled in GIC", irq_num);
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

// Platform API wrappers
int platform_irq_register(platform_t *platform, uint32_t irq_num,
                          void (*handler)(void *), void *context) {
  irq_register(platform, irq_num, handler, context);
  return 0; // Success
}

void platform_irq_enable(platform_t *platform, uint32_t irq_num) {
  irq_enable(platform, irq_num);
}
