// x64 Interrupt Handling
// IDT (Interrupt Descriptor Table) setup and interrupt handlers

#include "interrupt.h"
#include "timer.h"
#include "ioapic.h"
#include "io.h"
#include "printk.h"
#include <stdint.h>
#include <stddef.h>

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

// IRQ interrupts (vectors 33-47 for PCI IRQs)
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

// Spurious interrupt (vector 255)
extern void isr_stub_255(void);

// Forward declare interrupt handler
void interrupt_handler(uint64_t vector);

// IRQ routing table
#define MAX_IRQ_VECTORS 256

typedef struct {
    void* context;
    void (*handler)(void* context);
} irq_entry_t;

static irq_entry_t g_irq_table[MAX_IRQ_VECTORS];

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

// Exception names for debugging
static const char* exception_names[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack Fault", "General Protection", "Page Fault", "Reserved",
    "x87 FPU Error", "Alignment Check", "Machine Check", "SIMD Floating-Point",
    "Virtualization Exception", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security Exception", "Reserved"
};

// Common interrupt handler dispatcher
void interrupt_handler(uint64_t vector)
{
    if (vector == 32) {
        // Timer interrupt (vector 32)
        lapic_timer_handler();
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
static void pic_disable(void)
{
    // Mask all interrupts on both PICs
    // This prevents spurious interrupts from the legacy PIC
    outb(0x21, 0xFF);  // Master PIC: mask all
    outb(0xA1, 0xFF);  // Slave PIC: mask all

    printk("Legacy PIC disabled (all IRQs masked)\n");
}

// Initialize the IDT
void interrupt_init(void)
{
    // Disable legacy PIC (we use IOAPIC)
    pic_disable();

    // Initialize IOAPIC
    ioapic_init();

    // Array of exception handler pointers for vectors 0-31
    void (*exception_handlers[32])(void) = {
        isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3,
        isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7,
        isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11,
        isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15,
        isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19,
        isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23,
        isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27,
        isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
    };

    // Set up IDT pointer
    idtp.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idtp.base = (uint64_t)&idt;

    // Install exception handlers (vectors 0-31)
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, (uint64_t)exception_handlers[i], 0x08, 0x8E);
    }

    // Install timer interrupt handler (vector 32)
    idt_set_gate(32, (uint64_t)isr_stub_32, 0x08, 0x8E);

    // Install IRQ interrupt handlers (vectors 33-47)
    void (*irq_handlers[15])(void) = {
        isr_stub_33, isr_stub_34, isr_stub_35, isr_stub_36,
        isr_stub_37, isr_stub_38, isr_stub_39, isr_stub_40,
        isr_stub_41, isr_stub_42, isr_stub_43, isr_stub_44,
        isr_stub_45, isr_stub_46, isr_stub_47
    };

    for (int i = 0; i < 15; i++) {
        idt_set_gate(33 + i, (uint64_t)irq_handlers[i], 0x08, 0x8E);
    }

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

// Register IRQ handler
void irq_register(uint8_t vector, void (*handler)(void*), void* context)
{
    g_irq_table[vector].handler = handler;
    g_irq_table[vector].context = context;

    // Route the IRQ through IOAPIC
    // Vector 32 is timer (LAPIC, not IOAPIC)
    // Vectors 33+ are device IRQs
    if (vector >= 33) {
        uint8_t irq = vector - 32;
        uint8_t apic_id = 0;  // BSP APIC ID

        ioapic_route_irq(irq, vector, apic_id);
    }
}

// Enable (unmask) a specific IRQ
void irq_enable(uint8_t vector)
{
    // Unmask the IRQ in the IOAPIC
    // Vector 32 is timer (LAPIC), vectors 33+ are device IRQs (IOAPIC)
    if (vector >= 33) {
        uint8_t irq = vector - 32;
        ioapic_unmask_irq(irq);
    }
}

// Debug counters for irq_dispatch
static volatile uint32_t g_irq_dispatch_count = 0;
static volatile uint32_t g_irq_eoi_count = 0;

// Dispatch IRQ to registered handler
void irq_dispatch(uint8_t vector)
{
    g_irq_dispatch_count++;

    if (g_irq_table[vector].handler != NULL) {
        g_irq_table[vector].handler(g_irq_table[vector].context);
    }

    // Send EOI to LAPIC for all IRQs
    lapic_send_eoi();
    g_irq_eoi_count++;
}
