// x64 Platform Memory Configuration
// Defines MMIO regions and other platform-specific memory layout constants
//
// These values must match the early boot page table setup in boot.S

#ifndef PLATFORM_X64_CONFIG_H
#define PLATFORM_X64_CONFIG_H

// PCI MMIO Region (QEMU q35 machine type)
// Mapped in boot.S with 128 x 2 MiB huge pages
#define PCI_MMIO_BASE 0xC0000000ULL
#define PCI_MMIO_SIZE 0x10000000ULL // 256 MiB
#define PCI_MMIO_END (PCI_MMIO_BASE + PCI_MMIO_SIZE)

// High MMIO Region (Contains LAPIC, IOAPIC, VirtIO MMIO devices)
// Mapped in boot.S with 8 x 2 MiB huge pages starting at PD3[496]
#define HIGH_MMIO_BASE 0xFE000000ULL
#define HIGH_MMIO_SIZE 0x01000000ULL // 16 MiB
#define HIGH_MMIO_END (HIGH_MMIO_BASE + HIGH_MMIO_SIZE)

// VirtIO MMIO Base (QEMU microvm convention)
// Devices are typically at 0xFEB02A00 with 0x200 spacing
#define VIRTIO_MMIO_BASE 0xFEB00000ULL

// Number of huge pages for each MMIO region (for boot.S)
#define PCI_MMIO_HUGE_PAGES 128 // 128 x 2 MiB = 256 MiB
#define HIGH_MMIO_HUGE_PAGES 8  // 8 x 2 MiB = 16 MiB

// Page directory entry index for high MMIO region start
// PD3 covers 3-4GB (0xC0000000-0xFFFFFFFF)
// 0xFE000000 = 3GB + 992 MiB = PD entry 496 (496 * 2 MiB = 992 MiB)
#define HIGH_MMIO_PD_START_ENTRY 496

// Validation: Ensure high MMIO region stays below 4GB boundary
#if (HIGH_MMIO_END > 0x100000000ULL)
#error "High MMIO region exceeds 4GB boundary"
#endif

// Validation: Ensure PCI MMIO doesn't overlap with high MMIO
#if (PCI_MMIO_END > HIGH_MMIO_BASE)
#error "PCI MMIO region overlaps with high MMIO region"
#endif

#endif // PLATFORM_X64_CONFIG_H
