// Platform Interface
// Consolidated header for all platform/hardware interfaces
// This is the complete contract that platform implementations must provide

#pragma once

#include "kbase.h"
#include "kconfig.h"

// Forward declarations for types defined elsewhere
typedef struct kernel kernel_t;
typedef struct kwork kwork_t;
typedef struct platform platform_t;

// Each platform implements platform_impl.h with platform-specific types
// #include "platform_impl.h"

// ===========================================================================
// SECTION 1: Memory Management - Memory region discovery and management
// ===========================================================================

// Get list of available (free) memory regions
// Called after platform_init() completes
// Returns: linked list of free memory regions suitable for allocation
kregions_t platform_mem_regions(platform_t *platform);

// ===========================================================================
// SECTION 2: PCI - PCI configuration space access
// ===========================================================================

// PCI Configuration Space Access
// Each platform (x64, arm64 with PCIe, rv64 with PCIe) must implement these
// Platforms without PCI support should provide stubs that return error values

// Read from PCI configuration space
uint8_t platform_pci_config_read8(platform_t *platform, uint8_t bus,
                                  uint8_t slot, uint8_t func, uint8_t offset);
uint16_t platform_pci_config_read16(platform_t *platform, uint8_t bus,
                                    uint8_t slot, uint8_t func, uint8_t offset);
uint32_t platform_pci_config_read32(platform_t *platform, uint8_t bus,
                                    uint8_t slot, uint8_t func, uint8_t offset);

// Write to PCI configuration space
void platform_pci_config_write8(platform_t *platform, uint8_t bus, uint8_t slot,
                                uint8_t func, uint8_t offset, uint8_t value);
void platform_pci_config_write16(platform_t *platform, uint8_t bus,
                                 uint8_t slot, uint8_t func, uint8_t offset,
                                 uint16_t value);
void platform_pci_config_write32(platform_t *platform, uint8_t bus,
                                 uint8_t slot, uint8_t func, uint8_t offset,
                                 uint32_t value);

// Read Base Address Register (BAR)
// Returns the physical address mapped by the BAR, or 0 if BAR is not present
uint64_t platform_pci_read_bar(platform_t *platform, uint8_t bus, uint8_t slot,
                               uint8_t func, uint8_t bar_num);

// ===========================================================================
// SECTION 2A: MMIO - Memory-mapped I/O register access
// ===========================================================================

// MMIO register access with appropriate memory barriers
// These functions ensure proper synchronization on weakly-ordered architectures
// (ARM, RISC-V) while remaining efficient on strongly-ordered architectures
// (x86)
//
// Memory barriers ensure:
// - MMIO operations complete before proceeding
// - No speculative reads/writes to device registers
// - Proper ordering in weakly-ordered memory models
//
// Each platform implements these in platform/*/mmio.c

// Read from MMIO registers
uint8_t platform_mmio_read8(volatile uint8_t *addr);
uint16_t platform_mmio_read16(volatile uint16_t *addr);
uint32_t platform_mmio_read32(volatile uint32_t *addr);
static inline uint64_t platform_mmio_read64(volatile uint64_t *addr);

// Write to MMIO registers
void platform_mmio_write8(volatile uint8_t *addr, uint8_t val);
void platform_mmio_write16(volatile uint16_t *addr, uint16_t val);
void platform_mmio_write32(volatile uint32_t *addr, uint32_t val);
static inline void platform_mmio_write64(volatile uint64_t *addr, uint64_t val);

static inline void platform_mmio_barrier(void);

// ===========================================================================
// SECTION 3: Platform Lifecycle
// ===========================================================================

// Initialize platform-specific features (interrupts, timers, device
// enumeration) platform: platform state structure fdt: pointer to flattened
// device tree (may be NULL on platforms using other methods)
// kernel: pointer to kernel structure (platform can store as needed)
void platform_init(platform_t *platform, void *fdt, void *kernel);

// Wait for interrupt with timeout
// platform: platform state structure
// timeout_ms: timeout in milliseconds (UINT64_MAX = wait forever)
// Returns: current time in milliseconds after waking
uint64_t platform_wfi(platform_t *platform, uint64_t timeout_ms);

// Submit work and cancellations to platform (called from ktick)
// submissions: singly-linked list of work to submit (or NULL)
// cancellations: singly-linked list of work to cancel (or NULL)
void platform_submit(platform_t *platform, kwork_t *submissions,
                     kwork_t *cancellations);

// Abort system execution (shutdown/halt)
// Called when a fatal error occurs or assertion fails
// This function does not return
void platform_abort(void) __attribute__((noreturn));

// ===========================================================================
// SECTION 3A: Platform Debug - Register and stack dumps
// ===========================================================================

// Dump platform registers to debug console
// Platform-specific implementation (RIP/PC, SP, general purpose registers)
void platform_dump_registers(void);

// Dump stack contents to debug console
// bytes: number of bytes to dump from current stack pointer
void platform_dump_stack(uint32_t bytes);

// ===========================================================================
// SECTION 4: Interrupt Control
// ===========================================================================

// Enable interrupts globally
void platform_interrupt_enable(platform_t *platform);

// Disable interrupts globally
void platform_interrupt_disable(platform_t *platform);

// ===========================================================================
// SECTION 5: UART - Debug output
// ===========================================================================

// Output a null-terminated string to the debug console
void platform_uart_puts(const char *str);

// Output a single character to the debug console
void platform_uart_putc(char c);

// ===========================================================================
// SECTION 7: IRQ Management - Interrupt registration
// ===========================================================================

// Register an interrupt handler
// platform: platform state structure
// irq_num: platform-specific IRQ number
// handler: function to call when interrupt fires
// context: opaque context pointer passed to handler
// Returns: 0 on success, negative on error
int platform_irq_register(platform_t *platform, uint32_t irq_num,
                          void (*handler)(void *), void *context);

// Enable (unmask) a specific IRQ
// platform: platform state structure
// irq_num: platform-specific IRQ number
void platform_irq_enable(platform_t *platform, uint32_t irq_num);

// ===========================================================================
// SECTION 8: Work Submission - Process work queue changes
// ===========================================================================

// Submit work and cancellations to platform
// Called by kernel during tick processing with queued work
// platform: platform state structure
// submissions: singly-linked list of work to submit (or NULL)
// cancellations: singly-linked list of work to cancel (or NULL)
void platform_submit(platform_t *platform, kwork_t *submissions,
                     kwork_t *cancellations);

// Platform tick - called by kernel during tick processing
// Allows platform to process deferred interrupt work before callbacks run
// platform: platform state structure
// k: kernel state structure
void platform_tick(platform_t *platform, kernel_t *k);

// Release a network receive buffer back to the ring (for standing work)
// Called when user processes packet and wants to return buffer to device
// platform: platform state structure
// req: the standing receive request (knet_recv_req_t*, passed as void*)
// buffer_index: which buffer to release (0 to num_buffers-1)
void platform_net_buffer_release(platform_t *platform, void *req,
                                 size_t buffer_index);
