// x64 Interrupt Handling
// IDT (Interrupt Descriptor Table) setup and interrupt handlers

#include "interrupt.h"
#include "io.h"
#include "ioapic.h"
#include "platform_impl.h"
#include "printk.h"
#include "timer.h"
#include <stddef.h>
#include <stdint.h>

// Module-local platform pointer
// This is necessary because interrupt handlers (interrupt_handler,
// irq_dispatch) are called from assembly and cannot receive platform_t* as a
// parameter. This is an architectural limitation, not a design choice.
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

// IRQ interrupts (vectors 33-55 for device IRQs)
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

// Forward declare interrupt handler
void interrupt_handler(uint64_t vector);

// Set an IDT entry
static void idt_set_gate(platform_t *platform, uint8_t num, uint64_t handler,
                         uint16_t selector, uint8_t flags) {
  platform->idt[num].offset_low = handler & 0xFFFF;
  platform->idt[num].selector = selector;
  platform->idt[num].ist = 0;
  platform->idt[num].flags = flags;
  platform->idt[num].offset_mid = (handler >> 16) & 0xFFFF;
  platform->idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
  platform->idt[num].reserved = 0;
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

// Common interrupt handler dispatcher
void interrupt_handler(uint64_t vector) {
  if (vector == 32) {
    // Timer interrupt (vector 32)
    lapic_timer_handler(g_current_platform);
  } else if (vector == 255) {
    // Spurious interrupt - should not reach here (minimal stub)
    // Do nothing
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
    // IRQ - dispatch to registered handler
    irq_dispatch(vector);
  }
}

// Disable legacy PIC (8259) - we use IOAPIC instead
static void pic_disable(void) {
  // Mask all interrupts on both PICs
  // This prevents spurious interrupts from the legacy PIC
  outb(0x21, 0xFF); // Master PIC: mask all
  outb(0xA1, 0xFF); // Slave PIC: mask all

  printk("Legacy PIC disabled (all IRQs masked)\n");
}

// Initialize the IDT
void interrupt_init(platform_t *platform) {
  // Store platform pointer for ISR access
  g_current_platform = platform;

  // Disable legacy PIC (we use IOAPIC)
  pic_disable();

  // Initialize IOAPIC
  ioapic_init(platform);

  // Array of exception handler pointers for vectors 0-31
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
  platform->idtp.base = (uint64_t)&platform->idt;

  // Install exception handlers (vectors 0-31)
  for (int i = 0; i < 32; i++) {
    idt_set_gate(platform, i, (uint64_t)exception_handlers[i], 0x08, 0x8E);
  }

  // Install timer interrupt handler (vector 32)
  idt_set_gate(platform, 32, (uint64_t)isr_stub_32, 0x08, 0x8E);

  // Install IRQ interrupt handlers (vectors 33-55)
  void (*irq_handlers[23])(void) = {
      isr_stub_33, isr_stub_34, isr_stub_35, isr_stub_36, isr_stub_37,
      isr_stub_38, isr_stub_39, isr_stub_40, isr_stub_41, isr_stub_42,
      isr_stub_43, isr_stub_44, isr_stub_45, isr_stub_46, isr_stub_47,
      isr_stub_48, isr_stub_49, isr_stub_50, isr_stub_51, isr_stub_52,
      isr_stub_53, isr_stub_54, isr_stub_55};

  for (int i = 0; i < 23; i++) {
    idt_set_gate(platform, 33 + i, (uint64_t)irq_handlers[i], 0x08, 0x8E);
  }

  // Install spurious interrupt handler (vector 255)
  idt_set_gate(platform, 255, (uint64_t)isr_stub_255, 0x08, 0x8E);

  // Load IDT
  __asm__ volatile("lidt %0" : : "m"(platform->idtp));

  printk("IDT initialized (256 entries)\n");
}

// Enable interrupts
void platform_interrupt_enable(platform_t *platform) {
  (void)platform; // Unused on x64 (global CPU state)
  __asm__ volatile("sti");
}

// Disable interrupts
void platform_interrupt_disable(platform_t *platform) {
  (void)platform; // Unused on x64 (global CPU state)
  __asm__ volatile("cli");
}

// Register IRQ handler
// For MSI-X devices, just register the handler - no IOAPIC routing needed
// MSI-X messages go directly to LAPIC
void irq_register(platform_t *platform, uint8_t vector, void (*handler)(void *),
                  void *context) {
  platform->irq_table[vector].handler = handler;
  platform->irq_table[vector].context = context;
  // MSI-X: No IOAPIC routing - messages go directly to LAPIC
}

// Register IRQ handler for MMIO devices (edge-triggered)
void irq_register_mmio(platform_t *platform, uint8_t vector,
                       void (*handler)(void *), void *context) {
  platform->irq_table[vector].handler = handler;
  platform->irq_table[vector].context = context;

  // Route the MMIO IRQ through IOAPIC (edge-triggered)
  // Vector 32 is timer (LAPIC, not IOAPIC)
  // Vectors 33+ are device IRQs
  if (vector >= 33) {
    uint8_t irq = vector - 32;
    uint8_t apic_id = 0; // BSP APIC ID

    ioapic_route_mmio_irq(platform, irq, vector, apic_id);
  }
}

// Enable (unmask) a specific IRQ
void irq_enable(platform_t *platform, uint8_t vector) {
  // Unmask in IOAPIC for MMIO devices (vectors 33+)
  // MSI-X devices don't go through IOAPIC, so this is harmless for them
  if (vector >= 33) {
    uint8_t irq = vector - 32;
    ioapic_unmask_irq(platform, irq);
  }
}

// Dispatch IRQ to registered handler
void irq_dispatch(uint8_t vector) {
  g_current_platform->irq_dispatch_count++;

  if (g_current_platform->irq_table[vector].handler != NULL) {
    g_current_platform->irq_table[vector].handler(
        g_current_platform->irq_table[vector].context);
  }

  // Send EOI to LAPIC for all IRQs
  lapic_send_eoi(g_current_platform);
  g_current_platform->irq_eoi_count++;
}
