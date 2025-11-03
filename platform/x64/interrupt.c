// x64 Interrupt Handling
// IDT (Interrupt Descriptor Table) setup and interrupt handlers

#include "interrupt.h"
#include "ioapic.h"
#include "platform.h"
#include "platform_impl.h"
#include "printk.h"
#include <stdint.h>

// Module-local platform pointer
// REASON: interrupt_handler() and irq_dispatch() are called from assembly
// (isr_stubs.S) and cannot receive platform_t* as a parameter. This is an
// architectural limitation common to all platforms with exception vectors in
// assembly.
static platform_t *g_current_platform = NULL;

// External interrupt handler stubs (defined in assembly)
// Exception vectors 0-31
extern void isr_stub_0(void);
extern void isr_stub_1(void);
extern void isr_stub_2(void);
extern void isr_stub_3(void);
extern void isr_stub_4(void);
extern void isr_stub_5(void);
extern void isr_stub_6(void);
extern void isr_stub_7(void);
extern void isr_stub_8(void);
extern void isr_stub_9(void);
extern void isr_stub_10(void);
extern void isr_stub_11(void);
extern void isr_stub_12(void);
extern void isr_stub_13(void);
extern void isr_stub_14(void);
extern void isr_stub_15(void);
extern void isr_stub_16(void);
extern void isr_stub_17(void);
extern void isr_stub_18(void);
extern void isr_stub_19(void);
extern void isr_stub_20(void);
extern void isr_stub_21(void);
extern void isr_stub_22(void);
extern void isr_stub_23(void);
extern void isr_stub_24(void);
extern void isr_stub_25(void);
extern void isr_stub_26(void);
extern void isr_stub_27(void);
extern void isr_stub_28(void);
extern void isr_stub_29(void);
extern void isr_stub_30(void);
extern void isr_stub_31(void);

// Timer interrupt (vector 32)
extern void isr_stub_32(void);

// IRQ interrupts (vectors 33-55)
extern void isr_stub_33(void);
extern void isr_stub_34(void);
extern void isr_stub_35(void);
extern void isr_stub_36(void);
extern void isr_stub_37(void);
extern void isr_stub_38(void);
extern void isr_stub_39(void);
extern void isr_stub_40(void);
extern void isr_stub_41(void);
extern void isr_stub_42(void);
extern void isr_stub_43(void);
extern void isr_stub_44(void);
extern void isr_stub_45(void);
extern void isr_stub_46(void);
extern void isr_stub_47(void);
extern void isr_stub_48(void);
extern void isr_stub_49(void);
extern void isr_stub_50(void);
extern void isr_stub_51(void);
extern void isr_stub_52(void);
extern void isr_stub_53(void);
extern void isr_stub_54(void);
extern void isr_stub_55(void);

// Spurious interrupt (vector 255)
extern void isr_stub_255(void);

// Set an IDT entry
// Note: In x32, function pointers are 32-bit, but we need to convert to 64-bit
// for IDT
static void idt_set_gate(struct idt_entry *idt, uint8_t num, uint64_t handler,
                         uint16_t selector, uint8_t flags) {
  idt[num].offset_low = handler & 0xFFFF;
  idt[num].selector = selector;
  idt[num].ist = 0;
  idt[num].flags = flags;
  idt[num].offset_mid = (handler >> 16) & 0xFFFF;
  idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
  idt[num].reserved = 0;
}

// Exception names for debugging
static const char *exception_names[32] = {"Divide Error",
                                          "Debug",
                                          "NMI",
                                          "Breakpoint",
                                          "Overflow",
                                          "Bound Range Exceeded",
                                          "Invalid Opcode",
                                          "Device Not Available",
                                          "Double Fault",
                                          "Coprocessor Segment Overrun",
                                          "Invalid TSS",
                                          "Segment Not Present",
                                          "Stack Fault",
                                          "General Protection",
                                          "Page Fault",
                                          "Reserved",
                                          "x87 FPU Error",
                                          "Alignment Check",
                                          "Machine Check",
                                          "SIMD Floating-Point",
                                          "Virtualization Exception",
                                          "Control Protection",
                                          "Reserved",
                                          "Reserved",
                                          "Reserved",
                                          "Reserved",
                                          "Reserved",
                                          "Reserved",
                                          "Hypervisor Injection",
                                          "VMM Communication",
                                          "Security Exception",
                                          "Reserved"};

// Forward declaration
void lapic_timer_handler(platform_t *platform);

// Common interrupt handler dispatcher
void interrupt_handler(uint64_t vector) {
  if (vector == 32) {
    // Timer interrupt (vector 32)
    lapic_timer_handler(g_current_platform);
  } else if (vector == 255) {
    // Spurious interrupt - should not reach here (minimal stub)
  } else if (vector < 32) {
    // CPU exception - print diagnostic and halt
    printk("\n!!! EXCEPTION: ");
    printk(exception_names[vector]);
    printk(" (vector ");
    printk_dec(vector);
    printk(") !!!\n");
    printk("System halted.\n");
    // Halt the CPU
    while (1) {
      __asm__ volatile("hlt");
    }
  } else {
    // Dispatch to registered IRQ handlers
    irq_dispatch((uint8_t)vector);
  }
}

// Initialize the IDT
void interrupt_init(platform_t *platform) {
  // Save platform pointer for interrupt dispatch
  g_current_platform = platform;

  // Array of exception handler pointers for vectors 0-31
  // In x32, these are 32-bit pointers, but we need to convert to 64-bit for IDT
  void (*exception_handlers[32])(void) = {
      isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3,  isr_stub_4,
      isr_stub_5,  isr_stub_6,  isr_stub_7,  isr_stub_8,  isr_stub_9,
      isr_stub_10, isr_stub_11, isr_stub_12, isr_stub_13, isr_stub_14,
      isr_stub_15, isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19,
      isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23, isr_stub_24,
      isr_stub_25, isr_stub_26, isr_stub_27, isr_stub_28, isr_stub_29,
      isr_stub_30, isr_stub_31};

  // Set up IDT pointer
  platform->idtp.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
  // In x32, pointers are 32-bit but we store them in a 64-bit field
  // The cast through uintptr_t ensures correct conversion
  platform->idtp.base = (uint64_t)(uintptr_t)&platform->idt;

  // Install exception handlers (vectors 0-31)
  // Flags: 0x8E = Present, DPL=0, Interrupt Gate (64-bit)
  for (int i = 0; i < 32; i++) {
    // Convert 32-bit pointer to 64-bit address for IDT
    idt_set_gate(platform->idt, i, (uint64_t)(uintptr_t)exception_handlers[i],
                 0x08, 0x8E);
  }

  // Install timer interrupt handler (vector 32)
  idt_set_gate(platform->idt, 32, (uint64_t)(uintptr_t)isr_stub_32, 0x08, 0x8E);

  // Install IRQ interrupt handlers (vectors 33-55)
  idt_set_gate(platform->idt, 33, (uint64_t)(uintptr_t)isr_stub_33, 0x08, 0x8E);
  idt_set_gate(platform->idt, 34, (uint64_t)(uintptr_t)isr_stub_34, 0x08, 0x8E);
  idt_set_gate(platform->idt, 35, (uint64_t)(uintptr_t)isr_stub_35, 0x08, 0x8E);
  idt_set_gate(platform->idt, 36, (uint64_t)(uintptr_t)isr_stub_36, 0x08, 0x8E);
  idt_set_gate(platform->idt, 37, (uint64_t)(uintptr_t)isr_stub_37, 0x08, 0x8E);
  idt_set_gate(platform->idt, 38, (uint64_t)(uintptr_t)isr_stub_38, 0x08, 0x8E);
  idt_set_gate(platform->idt, 39, (uint64_t)(uintptr_t)isr_stub_39, 0x08, 0x8E);
  idt_set_gate(platform->idt, 40, (uint64_t)(uintptr_t)isr_stub_40, 0x08, 0x8E);
  idt_set_gate(platform->idt, 41, (uint64_t)(uintptr_t)isr_stub_41, 0x08, 0x8E);
  idt_set_gate(platform->idt, 42, (uint64_t)(uintptr_t)isr_stub_42, 0x08, 0x8E);
  idt_set_gate(platform->idt, 43, (uint64_t)(uintptr_t)isr_stub_43, 0x08, 0x8E);
  idt_set_gate(platform->idt, 44, (uint64_t)(uintptr_t)isr_stub_44, 0x08, 0x8E);
  idt_set_gate(platform->idt, 45, (uint64_t)(uintptr_t)isr_stub_45, 0x08, 0x8E);
  idt_set_gate(platform->idt, 46, (uint64_t)(uintptr_t)isr_stub_46, 0x08, 0x8E);
  idt_set_gate(platform->idt, 47, (uint64_t)(uintptr_t)isr_stub_47, 0x08, 0x8E);
  idt_set_gate(platform->idt, 48, (uint64_t)(uintptr_t)isr_stub_48, 0x08, 0x8E);
  idt_set_gate(platform->idt, 49, (uint64_t)(uintptr_t)isr_stub_49, 0x08, 0x8E);
  idt_set_gate(platform->idt, 50, (uint64_t)(uintptr_t)isr_stub_50, 0x08, 0x8E);
  idt_set_gate(platform->idt, 51, (uint64_t)(uintptr_t)isr_stub_51, 0x08, 0x8E);
  idt_set_gate(platform->idt, 52, (uint64_t)(uintptr_t)isr_stub_52, 0x08, 0x8E);
  idt_set_gate(platform->idt, 53, (uint64_t)(uintptr_t)isr_stub_53, 0x08, 0x8E);
  idt_set_gate(platform->idt, 54, (uint64_t)(uintptr_t)isr_stub_54, 0x08, 0x8E);
  idt_set_gate(platform->idt, 55, (uint64_t)(uintptr_t)isr_stub_55, 0x08, 0x8E);

  // Install spurious interrupt handler (vector 255)
  idt_set_gate(platform->idt, 255, (uint64_t)(uintptr_t)isr_stub_255, 0x08,
               0x8E);

  // Load IDT
  __asm__ volatile("lidt %0" : : "m"(platform->idtp));

  printk("IDT initialized (256 entries)\n");

  // Initialize IOAPIC
  ioapic_init(platform);
}

// Enable interrupts
void platform_interrupt_enable(platform_t *platform) {
  (void)platform; // Unused on x32
  __asm__ volatile("sti");
}

// Disable interrupts
void platform_interrupt_disable(platform_t *platform) {
  (void)platform; // Unused on x32
  __asm__ volatile("cli");
}

// Register IRQ handler (for MSI-X devices)
// MSI-X messages go directly to LAPIC, no IOAPIC routing needed
void irq_register(platform_t *platform, uint8_t irq_num,
                  void (*handler)(void *), void *context) {
  platform->irq_table[irq_num].handler = handler;
  platform->irq_table[irq_num].context = context;
  // MSI-X: No IOAPIC routing - messages go directly to LAPIC
}

// Register MMIO IRQ handler (edge-triggered, routes through IOAPIC)
// irq_line: The IRQ line from the device (0-23)
// handler/context: Handler to call when interrupt fires
// Note: IRQ table storage is handled by caller (platform_irq_register)
void irq_register_mmio(platform_t *platform, uint8_t irq_line,
                       void (*handler)(void *), void *context) {
  (void)handler;  // Handler stored by caller in IRQ table at vector index
  (void)context;  // Context stored by caller in IRQ table at vector index

  // Convert IRQ line to interrupt vector
  uint8_t vector = 32 + irq_line;

  // Route IRQ line through IOAPIC to CPU vector
  // MMIO: edge-triggered (trigger=0), active-high (polarity=0)
  ioapic_route_irq(platform, irq_line, vector, 0, 0, 0);
}

// Register PCI IRQ handler (level-triggered, routes through IOAPIC)
// irq_line: The IRQ line from the device (typically 16-23 for PCI)
// handler/context: Handler to call when interrupt fires
// Note: IRQ table storage is handled by caller (platform_irq_register)
void irq_register_pci(platform_t *platform, uint8_t irq_line,
                      void (*handler)(void *), void *context) {
  (void)handler;  // Handler stored by caller in IRQ table at vector index
  (void)context;  // Context stored by caller in IRQ table at vector index

  // Convert IRQ line to interrupt vector
  uint8_t vector = 32 + irq_line;

  // Route IRQ line through IOAPIC to CPU vector
  // PCI INTx: level-triggered (trigger=1), active-low (polarity=1)
  ioapic_route_irq(platform, irq_line, vector, 0, 1, 1);
}

// Enable (unmask) a specific IRQ
void irq_enable(platform_t *platform, uint8_t irq_num) {
  // Unmask in IOAPIC for MMIO devices
  // MSI-X devices don't go through IOAPIC, so this is harmless for them
  ioapic_unmask_irq(platform, irq_num);
}

// Global variable for debugging interrupts (printk not safe in IRQ context)
static volatile uint32_t g_last_irq_vector = 0;
static volatile uint32_t g_irq_count = 0;
volatile uint32_t g_msix_irq_count = 0; // Non-static for external access
static volatile uint32_t g_self_ipi_fired = 0; // Self-IPI test flag

// Dispatch IRQ (called from exception handler)
void irq_dispatch(uint8_t irq_num) {
  if (g_current_platform == NULL) {
    return;
  }

  // Track interrupt for debugging (printk not safe here)
  g_last_irq_vector = irq_num;
  g_irq_count++;

  // Track MSI-X interrupts (vectors 33-47)
  if (irq_num >= 33 && irq_num <= 47) {
    g_msix_irq_count++;
  }

  irq_entry_t *entry = &g_current_platform->irq_table[irq_num];
  if (entry->handler != NULL) {
    entry->handler(entry->context);
  }

  // Send EOI to Local APIC
  volatile uint32_t *lapic_eoi =
      (volatile uint32_t *)(uintptr_t)(g_current_platform->lapic_base + 0xB0);
  *lapic_eoi = 0;
}

// Self-IPI test handler (for vector 50)
static void self_ipi_test_handler(void *context) {
  (void)context;
  g_self_ipi_fired = 1;
}

// Test LAPIC interrupt delivery via self-IPI
void test_lapic_self_ipi(platform_t *platform) {
  printk("\n[LAPIC TEST] Sending self-IPI to vector 50...\n");

  // Register test handler for vector 50
  irq_register(platform, 50, self_ipi_test_handler, NULL);

  // Reset test flag
  g_self_ipi_fired = 0;

  // Send self-IPI using LAPIC ICR (Inter-processor Interrupt Command Register)
  // ICR Low is at offset 0x300, ICR High is at offset 0x310
  // For self-IPI: Destination = self (dest_shorthand=01b), delivery_mode=000b (fixed), vector=50
  volatile uint32_t *lapic_icr_high = (volatile uint32_t *)(uintptr_t)(platform->lapic_base + 0x310);
  volatile uint32_t *lapic_icr_low = (volatile uint32_t *)(uintptr_t)(platform->lapic_base + 0x300);

  // Write destination (not used for self-IPI, but clear it anyway)
  *lapic_icr_high = 0;

  // Write command: dest_shorthand=01b (self), delivery_mode=000b (fixed), vector=50
  // Bits: [18:19]=01 (self), [10:8]=000 (fixed), [7:0]=50 (vector)
  uint32_t icr_command = (1 << 18) | 50;
  *lapic_icr_low = icr_command;

  // Wait briefly for interrupt to fire (should be nearly instant)
  // Use a simple delay loop
  for (volatile int i = 0; i < 1000000; i++) {
    if (g_self_ipi_fired) {
      break;
    }
  }

  if (g_self_ipi_fired) {
    printk("[LAPIC TEST] SUCCESS: Self-IPI delivered! LAPIC→CPU path is working.\n\n");
  } else {
    printk("[LAPIC TEST] FAILED: Self-IPI did not fire. LAPIC or IDT issue!\n\n");
  }

  // Unregister test handler
  platform->irq_table[50].handler = NULL;
  platform->irq_table[50].context = NULL;
}
