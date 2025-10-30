// Platform Interface
// Consolidated header for all platform/hardware interfaces
// This is the complete contract that platform implementations must provide

#pragma once

#include "kbase.h"
#include <stddef.h>
#include <stdint.h>

// Forward declarations for types defined elsewhere
typedef struct kernel kernel_t;
typedef struct kwork kwork_t;

// Each platform implements platform_impl.h with platform-specific types
#include "platform_impl.h"

// ===========================================================================
// SECTION 1: Device Tree (FDT) - Flattened Device Tree structures/functions
// ===========================================================================

// FDT magic number
#define FDT_MAGIC 0xd00dfeed

// FDT structure block tokens
#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE 0x00000002
#define FDT_PROP 0x00000003
#define FDT_NOP 0x00000004
#define FDT_END 0x00000009

// FDT header structure (all fields are big-endian)
struct fdt_header {
  uint32_t magic;             // Magic number: 0xd00dfeed
  uint32_t totalsize;         // Total size of FDT in bytes
  uint32_t off_dt_struct;     // Offset to structure block
  uint32_t off_dt_strings;    // Offset to strings block
  uint32_t off_mem_rsvmap;    // Offset to memory reservation block
  uint32_t version;           // FDT version
  uint32_t last_comp_version; // Last compatible version
  uint32_t boot_cpuid_phys;   // Boot CPU ID
  uint32_t size_dt_strings;   // Size of strings block
  uint32_t size_dt_struct;    // Size of structure block
};

// FDT property structure (in structure block)
struct fdt_prop {
  uint32_t len;     // Length of property value in bytes
  uint32_t nameoff; // Offset into strings block
};

// FDT memory reservation entry
struct fdt_reserve_entry {
  uint64_t address;
  uint64_t size;
};

// VirtIO MMIO device info
typedef struct {
  uint64_t base_addr;
  uint64_t size;
  uint32_t irq;
} virtio_mmio_device_t;

// FDT parsing functions
void platform_fdt_dump(platform_t *platform, void *fdt);

// Find VirtIO MMIO devices in device tree
// Returns: number of devices found (up to max_devices)
int platform_fdt_find_virtio_mmio(platform_t *platform, void *fdt,
                                  virtio_mmio_device_t *devices,
                                  int max_devices);

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

// Forward declaration (actual type in kapi.h)
typedef struct kwork kwork_t;
typedef struct kernel kernel_t;

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
