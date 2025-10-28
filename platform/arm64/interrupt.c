// ARM64 Interrupt Handling
// Exception vector setup and GIC (Generic Interrupt Controller) initialization

#include "interrupt.h"
#include "printk.h"
#include <stdint.h>
#include <stddef.h>

// GIC-400 (GICv2) base addresses for QEMU virt machine
// These are the standard addresses used by QEMU's virt machine
#define GICD_BASE 0x08000000  // Distributor base
#define GICC_BASE 0x08010000  // CPU interface base

// GIC Distributor registers
#define GICD_CTLR       (GICD_BASE + 0x000)
#define GICD_TYPER      (GICD_BASE + 0x004)
#define GICD_ISENABLER  (GICD_BASE + 0x100)
#define GICD_ICENABLER  (GICD_BASE + 0x180)
#define GICD_IPRIORITYR (GICD_BASE + 0x400)
#define GICD_ITARGETSR  (GICD_BASE + 0x800)
#define GICD_ICFGR      (GICD_BASE + 0xC00)

// GIC CPU Interface registers
#define GICC_CTLR  (GICC_BASE + 0x000)
#define GICC_PMR   (GICC_BASE + 0x004)
#define GICC_IAR   (GICC_BASE + 0x00C)
#define GICC_EOIR  (GICC_BASE + 0x010)

// ARM Generic Timer physical interrupt (PPI 14 = IRQ 30)
#define TIMER_IRQ 30

// Maximum number of IRQs supported by GICv2
#define MAX_IRQS 1024

// Memory-mapped register access helpers
static inline void mmio_write32(uint64_t addr, uint32_t value)
{
    *(volatile uint32_t*)addr = value;
}

static inline uint32_t mmio_read32(uint64_t addr)
{
    return *(volatile uint32_t*)addr;
}

// Forward declare timer handler
void generic_timer_handler(void);

// Forward declare exception handler (called from vectors.S)
void exception_handler(uint64_t type, uint64_t esr, uint64_t elr, uint64_t far);

// IRQ handler table entry
typedef struct {
    void* context;
    void (*handler)(void* context);
} irq_entry_t;

// IRQ dispatch table
static irq_entry_t g_irq_table[MAX_IRQS];

// External exception vector table (defined in vectors.S)
extern void exception_vector_table(void);

// Exception names for debugging
static const char* exception_names[] = {
    "Synchronous EL1t",
    "IRQ EL1t",
    "FIQ EL1t",
    "SError EL1t",
    "Synchronous EL1h",
    "IRQ EL1h",
    "FIQ EL1h",
    "SError EL1h",
    "Synchronous 64-bit EL0",
    "IRQ 64-bit EL0",
    "FIQ 64-bit EL0",
    "SError 64-bit EL0",
    "Synchronous 32-bit EL0",
    "IRQ 32-bit EL0",
    "FIQ 32-bit EL0",
    "SError 32-bit EL0"
};

// Common exception handler dispatcher
// Called from assembly exception handlers
void exception_handler(uint64_t type, uint64_t esr, uint64_t elr, uint64_t far)
{
    // Type encoding (from assembly):
    // 0 = Synchronous from current EL with SP_EL0
    // 1 = IRQ from current EL with SP_EL0
    // 2 = FIQ from current EL with SP_EL0
    // 3 = SError from current EL with SP_EL0
    // 4 = Synchronous from current EL with SP_ELx
    // 5 = IRQ from current EL with SP_ELx
    // 6 = FIQ from current EL with SP_ELx
    // 7 = SError from current EL with SP_ELx
    // ... and so on

    if (type == 5) {  // IRQ from current EL with SP_ELx (most common for kernel)
        // Read interrupt acknowledge register to get interrupt ID
        uint32_t irq = mmio_read32(GICC_IAR) & 0x3FF;

        if (irq == TIMER_IRQ) {
            // Timer interrupt - call timer handler
            generic_timer_handler();
        } else if (irq >= 1020) {
            // Spurious interrupt (1023) or special value
            // Don't send EOI for spurious interrupts
            return;
        } else {
            // Dispatch to registered handler
            irq_dispatch(irq);
        }

        // Send End of Interrupt
        mmio_write32(GICC_EOIR, irq);
    } else {
        // Unhandled exception type - print diagnostic and halt
        printk("\n!!! EXCEPTION: ");
        if (type < sizeof(exception_names) / sizeof(exception_names[0])) {
            printk(exception_names[type]);
        } else {
            printk("Unknown");
        }
        printk(" (type ");
        printk_dec(type);
        printk(") !!!\n");
        printk("ESR_EL1: 0x");
        printk_hex64(esr);
        printk("\n");
        printk("ELR_EL1: 0x");
        printk_hex64(elr);
        printk("\n");
        printk("FAR_EL1: 0x");
        printk_hex64(far);
        printk("\n");
        printk("System halted.\n");

        // Halt the CPU
        while (1) {
            __asm__ volatile("wfe");
        }
    }
}

// Initialize GIC (Generic Interrupt Controller)
static void gic_init(void)
{
    // Initialize GIC Distributor
    // Disable distributor during configuration
    mmio_write32(GICD_CTLR, 0);

    // Read number of interrupt lines
    uint32_t typer = mmio_read32(GICD_TYPER);
    uint32_t num_lines = ((typer & 0x1F) + 1) * 32;

    printk("GIC Distributor: ");
    printk_dec(num_lines);
    printk(" interrupt lines\n");

    // Disable all interrupts
    for (uint32_t i = 0; i < num_lines; i += 32) {
        mmio_write32(GICD_ICENABLER + (i / 32) * 4, 0xFFFFFFFF);
    }

    // Set priority for all interrupts to a default value
    for (uint32_t i = 0; i < num_lines; i += 4) {
        mmio_write32(GICD_IPRIORITYR + i, 0xA0A0A0A0);
    }

    // Set all SPIs (IRQs 32+) to target CPU 0
    // PPIs (IRQs 16-31) are hardwired to their CPU, so start from IRQ 32
    // Each 32-bit register contains targets for 4 interrupts (8 bits each)
    printk("Setting SPI targets to CPU 0...\n");
    for (uint32_t i = 32; i < num_lines; i += 4) {
        mmio_write32(GICD_ITARGETSR + i, 0x01010101);  // All 4 IRQs target CPU 0
    }

    // Verify IRQ 79 target was set
    uint32_t irq79_target_reg = GICD_ITARGETSR + (79 / 4) * 4;
    uint32_t irq79_target = mmio_read32(irq79_target_reg);
    printk("IRQ 79 target register value: 0x");
    printk_hex32(irq79_target);
    printk("\n");

    // Configure timer interrupt (IRQ 30)
    // Set target to CPU 0
    uint32_t target_reg = GICD_ITARGETSR + (TIMER_IRQ / 4) * 4;
    uint32_t target_shift = (TIMER_IRQ % 4) * 8;
    uint32_t target_val = mmio_read32(target_reg);
    target_val &= ~(0xFF << target_shift);
    target_val |= (0x01 << target_shift);  // Target CPU 0
    mmio_write32(target_reg, target_val);

    // Enable timer interrupt
    mmio_write32(GICD_ISENABLER + (TIMER_IRQ / 32) * 4, 1 << (TIMER_IRQ % 32));

    // Enable distributor
    mmio_write32(GICD_CTLR, 1);

    // Initialize GIC CPU Interface
    // Set priority mask to allow all priorities
    mmio_write32(GICC_PMR, 0xFF);

    // Enable CPU interface
    mmio_write32(GICC_CTLR, 1);

    printk("GIC initialized (Distributor at 0x");
    printk_hex64(GICD_BASE);
    printk(", CPU Interface at 0x");
    printk_hex64(GICC_BASE);
    printk(")\n");
}

// Initialize exception vectors and GIC
void interrupt_init(void)
{
    // Install exception vector table
    // VBAR_EL1 holds the base address of exception vectors
    uint64_t vbar = (uint64_t)exception_vector_table;
    __asm__ volatile("msr vbar_el1, %0" : : "r"(vbar));
    __asm__ volatile("isb");

    printk("Exception vectors installed at 0x");
    printk_hex64(vbar);
    printk("\n");

    // Initialize GIC
    gic_init();
}

// Enable interrupts (unmask IRQ and FIQ in DAIF)
void interrupt_enable(void)
{
    // Clear I (IRQ mask) and F (FIQ mask) bits in DAIF
    // DAIF bits: D=Debug, A=SError, I=IRQ, F=FIQ
    __asm__ volatile("msr daifclr, #0x3");  // Clear I and F bits
    __asm__ volatile("isb");
}

// Disable interrupts (mask IRQ and FIQ in DAIF)
void interrupt_disable(void)
{
    // Set I (IRQ mask) and F (FIQ mask) bits in DAIF
    __asm__ volatile("msr daifset, #0x3");  // Set I and F bits
    __asm__ volatile("isb");
}

// Configure interrupt trigger type (level/edge)
// trigger: 0 = level-sensitive, 1 = edge-triggered
static void irq_set_trigger(uint32_t irq_num, uint32_t trigger)
{
    if (irq_num >= MAX_IRQS) {
        return;
    }

    // GICD_ICFGR: 2 bits per interrupt
    // Bit [2n+1]: 0 = level-sensitive, 1 = edge-triggered
    // Bit [2n]: reserved (model-specific)
    uint32_t reg = GICD_ICFGR + (irq_num / 16) * 4;
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
void irq_register(uint32_t irq_num, void (*handler)(void*), void* context)
{
    if (irq_num >= MAX_IRQS) {
        return;
    }

    g_irq_table[irq_num].handler = handler;
    g_irq_table[irq_num].context = context;

    // VirtIO MMIO interrupts are edge-triggered (from device tree)
    // Configure as edge-triggered for all non-timer IRQs
    if (irq_num != TIMER_IRQ) {
        irq_set_trigger(irq_num, 1);  // 1 = edge-triggered
    }

    printk("IRQ ");
    printk_dec(irq_num);
    printk(" registered (");
    printk(irq_num == TIMER_IRQ ? "level" : "edge");
    printk("-triggered, target CPU 0)\n");
}

// Enable (unmask) a specific IRQ in the GIC
void irq_enable(uint32_t irq_num)
{
    if (irq_num >= MAX_IRQS) {
        return;
    }

    // Enable interrupt in GIC Distributor
    mmio_write32(GICD_ISENABLER + (irq_num / 32) * 4, 1 << (irq_num % 32));

    printk("IRQ ");
    printk_dec(irq_num);
    printk(" enabled in GIC\n");
}

// Dispatch IRQ to registered handler
void irq_dispatch(uint32_t irq_num)
{
    if (irq_num >= MAX_IRQS) {
        return;
    }

    if (g_irq_table[irq_num].handler != NULL) {
        g_irq_table[irq_num].handler(g_irq_table[irq_num].context);
    }
}

// Dump GIC configuration for a specific IRQ (for debugging)
void irq_dump_config(uint32_t irq_num)
{
    if (irq_num >= MAX_IRQS) {
        return;
    }

    printk("GIC configuration for IRQ ");
    printk_dec(irq_num);
    printk(":\n");

    // Check if enabled
    uint32_t enable_reg = GICD_ISENABLER + (irq_num / 32) * 4;
    uint32_t enable_val = mmio_read32(enable_reg);
    uint32_t enabled = (enable_val >> (irq_num % 32)) & 1;
    printk("  Enabled: ");
    printk(enabled ? "yes" : "no");
    printk("\n");

    // Check priority (4 interrupts per 32-bit register, 8 bits each)
    uint32_t priority_reg = GICD_IPRIORITYR + (irq_num / 4) * 4;
    uint32_t priority_shift = (irq_num % 4) * 8;
    uint32_t priority = (mmio_read32(priority_reg) >> priority_shift) & 0xFF;
    printk("  Priority: 0x");
    printk_hex8(priority);
    printk("\n");

    // Check target CPU
    uint32_t target_reg = GICD_ITARGETSR + (irq_num / 4) * 4;
    uint32_t target_shift = (irq_num % 4) * 8;
    uint32_t target = (mmio_read32(target_reg) >> target_shift) & 0xFF;
    printk("  Target CPU mask: 0x");
    printk_hex8(target);
    printk("\n");

    // Check trigger type
    uint32_t cfg_reg = GICD_ICFGR + (irq_num / 16) * 4;
    uint32_t cfg_shift = (irq_num % 16) * 2;
    uint32_t cfg = (mmio_read32(cfg_reg) >> cfg_shift) & 0x3;
    printk("  Trigger: ");
    printk((cfg & 0x2) ? "edge" : "level");
    printk("\n");

    // Check GIC CPU interface state
    uint32_t gicc_ctlr = mmio_read32(GICC_CTLR);
    uint32_t gicc_pmr = mmio_read32(GICC_PMR);
    printk("  GICC_CTLR: 0x");
    printk_hex32(gicc_ctlr);
    printk(", GICC_PMR: 0x");
    printk_hex32(gicc_pmr);
    printk("\n");

    // Check GIC Distributor state
    uint32_t gicd_ctlr = mmio_read32(GICD_CTLR);
    printk("  GICD_CTLR: 0x");
    printk_hex32(gicd_ctlr);
    printk("\n");
}
