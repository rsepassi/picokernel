// x32 Interrupt Handling
// IDT (Interrupt Descriptor Table) setup and interrupt handlers

#include "interrupt.h"
#include "printk.h"
#include <stdint.h>

#define IDT_ENTRIES 256

// IDT Gate descriptor structure
// Note: Even in x32 mode, IDT entries are 64-bit mode descriptors
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

// Spurious interrupt (vector 255)
extern void isr_stub_255(void);

// Forward declare timer handler
void lapic_timer_handler(void);

// Set an IDT entry
// Note: In x32, function pointers are 32-bit, but we need to convert to 64-bit for IDT
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
        // Unhandled interrupt - for now just ignore
        // (could add logging here if needed)
    }
}

// Initialize the IDT
void interrupt_init(void)
{
    // Array of exception handler pointers for vectors 0-31
    // In x32, these are 32-bit pointers, but we need to convert to 64-bit for IDT
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
    // In x32, pointers are 32-bit but we store them in a 64-bit field
    // The cast through uintptr_t ensures correct conversion
    idtp.base = (uint64_t)(uintptr_t)&idt;

    // Install exception handlers (vectors 0-31)
    // Flags: 0x8E = Present, DPL=0, Interrupt Gate (64-bit)
    for (int i = 0; i < 32; i++) {
        // Convert 32-bit pointer to 64-bit address for IDT
        idt_set_gate(i, (uint64_t)(uintptr_t)exception_handlers[i], 0x08, 0x8E);
    }

    // Install timer interrupt handler (vector 32)
    idt_set_gate(32, (uint64_t)(uintptr_t)isr_stub_32, 0x08, 0x8E);

    // Install spurious interrupt handler (vector 255)
    idt_set_gate(255, (uint64_t)(uintptr_t)isr_stub_255, 0x08, 0x8E);

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
