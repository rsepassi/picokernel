// x64 Interrupt Handling
// IDT (Interrupt Descriptor Table) setup and interrupt handlers

#include "interrupt.h"
#include "printk.h"
#include <stdint.h>

#define IDT_ENTRIES 256

// IDT Gate descriptor structure
struct idt_entry {
    uint16_t offset_low;    // Offset bits 0-15
    uint16_t selector;      // Code segment selector
    uint8_t  ist;           // Interrupt Stack Table (bits 0-2), reserved (bits 3-7)
    uint8_t  flags;         // Type and attributes
    uint16_t offset_mid;    // Offset bits 16-31
    uint32_t offset_high;   // Offset bits 32-63
    uint32_t reserved;      // Reserved (must be zero)
} __attribute__((packed));

// IDT Pointer structure
struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// IDT table and pointer
static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;

// External interrupt handler stubs (defined in assembly)
extern void isr_stub_common(void);
extern void isr_stub_32(void);
extern void isr_stub_255(void);

// Forward declare timer handler
void lapic_timer_handler(void);

// Set an IDT entry
static void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t flags)
{
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;
    idt[num].flags = flags;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].reserved = 0;
}

// Common interrupt handler dispatcher
void interrupt_handler(uint64_t vector)
{
    if (vector == 32) {
        // Timer interrupt (vector 32)
        lapic_timer_handler();
    } else if (vector == 255) {
        // Spurious interrupt - just ignore, no EOI needed
    } else {
        // Unhandled interrupt - for now just ignore
        // (printk in interrupt context may not be safe)
    }
}

// Initialize the IDT
void interrupt_init(void)
{
    // Set up IDT pointer
    idtp.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idtp.base = (uint64_t)&idt;

    // Install default handler for all exception vectors (0-31)
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, (uint64_t)isr_stub_common, 0x08, 0x8E);
    }

    // Install timer interrupt handler (vector 32)
    // Flags: 0x8E = Present, DPL=0, Interrupt Gate (64-bit)
    idt_set_gate(32, (uint64_t)isr_stub_32, 0x08, 0x8E);

    // Install spurious interrupt handler (vector 255)
    idt_set_gate(255, (uint64_t)isr_stub_255, 0x08, 0x8E);

    // Load IDT
    __asm__ volatile("lidt %0" : : "m"(idtp));

    printk("IDT initialized (256 entries)\n");
}

// Enable interrupts
void interrupt_enable(void)
{
    __asm__ volatile("sti");
}

// Disable interrupts
void interrupt_disable(void)
{
    __asm__ volatile("cli");
}
