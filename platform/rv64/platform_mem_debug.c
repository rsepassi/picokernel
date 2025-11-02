// RISC-V 64 Platform Memory Debugging Implementation
// Implements kernel/mem_debug.h interface for RV64

#include "mem_debug.h"
#include "platform.h"
#include "printk.h"

#ifdef KDEBUG

// RISC-V 64 memory layout constants (QEMU virt machine)
#define DTB_BASE 0x80000000ULL    // DTB passed via a1 register
#define DTB_SIZE 0x00200000ULL    // 2 MiB reserved (typical)
#define RAM_BASE 0x80000000ULL    // RAM starts here
#define RAM_SIZE 0x08000000ULL    // 128 MiB default (for display purposes)

// Linker-provided symbols
extern uint8_t _start[];
extern uint8_t _text_start[], _text_end[];
extern uint8_t _rodata_start[], _rodata_end[];
extern uint8_t _data_start[], _data_end[];
extern uint8_t _bss_start[], _bss_end[];
extern uint8_t __bss_start[], __bss_end[];
extern uint8_t _end[];
extern uint8_t stack_bottom[], stack_top[];

// Validate critical memory regions early in boot (post-boot.S)
void platform_mem_validate_critical(void) {
  printk("\n[MEM] === RISC-V 64 Early Boot Validation ===\n");
  bool all_ok = true;

  // 1. Validate DTB region
  printk("[MEM] DTB region: 0x");
  printk_hex64(DTB_BASE);
  printk(" - 0x");
  printk_hex64(DTB_BASE + DTB_SIZE - 1);
  printk(" (reserved for device tree)\n");

  // 2. Validate kernel sections
  uintptr_t text_start = (uintptr_t)_text_start;
  uintptr_t text_end = (uintptr_t)_text_end;
  uintptr_t rodata_start = (uintptr_t)_rodata_start;
  uintptr_t rodata_end = (uintptr_t)_rodata_end;
  uintptr_t data_start = (uintptr_t)_data_start;
  uintptr_t data_end = (uintptr_t)_data_end;
  uintptr_t bss_start = (uintptr_t)_bss_start;
  uintptr_t bss_end = (uintptr_t)_bss_end;
  uintptr_t kernel_end = (uintptr_t)_end;

  printk("[MEM] .text:   0x");
  printk_hex64(text_start);
  printk(" - 0x");
  printk_hex64(text_end);
  uint32_t text_size = text_end - text_start;
  printk(" (");
  printk_dec(text_size);
  printk(" bytes)\n");

  printk("[MEM] .rodata: 0x");
  printk_hex64(rodata_start);
  printk(" - 0x");
  printk_hex64(rodata_end);
  uint32_t rodata_size = rodata_end - rodata_start;
  printk(" (");
  printk_dec(rodata_size);
  printk(" bytes)\n");

  printk("[MEM] .data:   0x");
  printk_hex64(data_start);
  printk(" - 0x");
  printk_hex64(data_end);
  uint32_t data_size = data_end - data_start;
  printk(" (");
  printk_dec(data_size);
  printk(" bytes)\n");

  printk("[MEM] .bss:    0x");
  printk_hex64(bss_start);
  printk(" - 0x");
  printk_hex64(bss_end);
  uint32_t bss_size = bss_end - bss_start;
  printk(" (");
  printk_dec(bss_size);
  printk(" bytes)\n");

  // 3. Verify kernel base address
  uintptr_t kernel_base = (uintptr_t)_start;
  if (text_start != kernel_base) {
    printk("[MEM] ERROR: Kernel .text not at expected base 0x");
    printk_hex64(kernel_base);
    printk(" (at 0x");
    printk_hex64(text_start);
    printk(")\n");
    kpanic("Kernel base address corruption detected");
    all_ok = false;
  }

  // 4. Check sections don't overlap DTB
  if (kmem_ranges_overlap(DTB_BASE, DTB_SIZE, text_start,
                          kernel_end - text_start)) {
    printk("[MEM] ERROR: Kernel overlaps with DTB region!\n");
    kpanic("Kernel/DTB memory overlap detected");
    all_ok = false;
  }

  // 5. Validate stack
  uintptr_t stack_bot = (uintptr_t)stack_bottom;
  uintptr_t stack_t = (uintptr_t)stack_top;
  uint32_t stack_size = stack_t - stack_bot;

  printk("[MEM] Stack:   0x");
  printk_hex64(stack_bot);
  printk(" - 0x");
  printk_hex64(stack_t);
  printk(" (");
  printk_dec(stack_size);
  printk(" bytes, grows down)\n");

  if (stack_size != 16384) {
    printk("[MEM] WARNING: Stack size is not 16 KiB as expected\n");
  }

  // 6. Check BSS is zeroed (sample check - first and last 64 bytes)
  bool bss_zeroed = kmem_validate_pattern((void *)bss_start, 64, 0x00);
  if (bss_end - bss_start > 128) {
    bss_zeroed &= kmem_validate_pattern((void *)(bss_end - 64), 64, 0x00);
  }

  if (!bss_zeroed) {
    printk("[MEM] ERROR: BSS not properly zeroed by boot.S\n");
    kpanic("BSS section not properly zeroed");
    all_ok = false;
  } else {
    printk("[MEM] BSS zeroing: OK\n");
  }

  // 7. Check sections are properly ordered and aligned
  if (text_end > rodata_start || rodata_end > data_start ||
      data_end > bss_start) {
    printk("[MEM] ERROR: Kernel sections not properly ordered\n");
    kpanic("Kernel sections improperly ordered");
    all_ok = false;
  }

  printk("[MEM] Total kernel size: ");
  printk_dec(kernel_end - text_start);
  printk(" bytes\n");

  // 8. Verify .text and .rodata checksums
  printk("[MEM] Verifying section checksums...\n");

  uint32_t expected_text_crc = platform_get_expected_text_checksum();
  uint32_t expected_rodata_crc = platform_get_expected_rodata_checksum();

  if (expected_text_crc == 0 && expected_rodata_crc == 0) {
    printk("[MEM] WARNING: Expected checksums not set (build may not have run "
           "checksum script)\n");
  } else {
    uint32_t actual_text_crc = kmem_checksum_section(_text_start, _text_end);
    uint32_t actual_rodata_crc =
        kmem_checksum_section(_rodata_start, _rodata_end);

    printk("[MEM] .text checksum:   expected=0x");
    printk_hex32(expected_text_crc);
    printk(", actual=0x");
    printk_hex32(actual_text_crc);
    if (actual_text_crc == expected_text_crc) {
      printk(" OK\n");
    } else {
      printk(" MISMATCH!\n");
      kpanic(".text section checksum mismatch - code corruption detected");
      all_ok = false;
    }

    printk("[MEM] .rodata checksum: expected=0x");
    printk_hex32(expected_rodata_crc);
    printk(", actual=0x");
    printk_hex32(actual_rodata_crc);
    if (actual_rodata_crc == expected_rodata_crc) {
      printk(" OK\n");
    } else {
      printk(" MISMATCH!\n");
      kpanic(".rodata section checksum mismatch - data corruption detected");
      all_ok = false;
    }
  }

  if (all_ok) {
    printk("[MEM] === Early Boot Validation PASSED ===\n\n");
  } else {
    printk("[MEM] === Early Boot Validation FAILED ===\n\n");
  }
}

// Validate post-initialization state (called after kmain_init)
void platform_mem_validate_post_init(platform_t *platform, void *fdt) {
  printk("\n[MEM] === RISC-V 64 Post-Init Validation ===\n");
  bool all_ok = true;

  // 1. Validate DTB pointer
  uintptr_t fdt_addr = (uintptr_t)fdt;
  printk("[MEM] DTB pointer: 0x");
  printk_hex64(fdt_addr);

  if (fdt_addr < RAM_BASE || fdt_addr >= RAM_BASE + DTB_SIZE) {
    printk(" (WARNING: not within expected DTB region)\n");
  } else {
    printk(" (OK)\n");
  }

  // 2. Validate MMIO regions are accessible (basic probe)
  // Note: These are fixed addresses for QEMU virt machine, not from linker
  volatile uint32_t *uart_base = (volatile uint32_t *)0x10000000UL;
  volatile uint32_t *plic_base = (volatile uint32_t *)0x0c000000UL;
  volatile uint32_t *virtio_base = (volatile uint32_t *)0x10001000UL;

  printk("[MEM] UART (0x10000000): ");
  uint32_t uart_val = *uart_base;
  (void)uart_val; // Mark as used
  printk("accessible\n");

  printk("[MEM] PLIC (0x0c000000): ");
  uint32_t plic_val = *plic_base;
  (void)plic_val; // Mark as used
  printk("accessible\n");

  printk("[MEM] VirtIO MMIO (0x10001000): ");
  uint32_t virtio_val = *virtio_base;
  (void)virtio_val; // Mark as used
  printk("accessible\n");

  // 3. Validate platform timebase frequency was initialized
  if (platform->timebase_freq == 0) {
    printk("[MEM] ERROR: Timebase frequency not initialized\n");
    kpanic("Platform timer not initialized");
    all_ok = false;
  } else {
    printk("[MEM] Timebase frequency: ");
    printk_dec(platform->timebase_freq);
    printk(" Hz\n");
  }

  // 4. Report device discoveries
  printk("[MEM] VirtIO RNG device: ");
  if (platform->virtio_rng_ptr != NULL) {
    printk("found\n");
  } else {
    printk("not found\n");
  }

  printk("[MEM] VirtIO BLK device: ");
  if (platform->has_block_device) {
    printk("found (");
    printk_dec(platform->block_sector_size);
    printk(" byte sectors, ");
    printk_dec(
        (uint32_t)(platform->block_capacity >> 10)); // Convert to K sectors
    printk("K sectors)\n");
  } else {
    printk("not found\n");
  }

  printk("[MEM] VirtIO NET device: ");
  if (platform->has_net_device) {
    printk("found (MAC: ");
    for (int i = 0; i < 6; i++) {
      printk_hex8(platform->net_mac_address[i]);
      if (i < 5)
        printk(":");
    }
    printk(")\n");
  } else {
    printk("not found\n");
  }

  // 5. Re-verify checksums after initialization
  printk("[MEM] Re-verifying section checksums after init...\n");

  uint32_t expected_text_crc = platform_get_expected_text_checksum();
  uint32_t expected_rodata_crc = platform_get_expected_rodata_checksum();

  if (expected_text_crc != 0 || expected_rodata_crc != 0) {
    extern uint8_t _text_start[], _text_end[];
    extern uint8_t _rodata_start[], _rodata_end[];

    uint32_t actual_text_crc = kmem_checksum_section(_text_start, _text_end);
    uint32_t actual_rodata_crc =
        kmem_checksum_section(_rodata_start, _rodata_end);

    printk("[MEM] .text checksum:   expected=0x");
    printk_hex32(expected_text_crc);
    printk(", actual=0x");
    printk_hex32(actual_text_crc);
    if (actual_text_crc == expected_text_crc) {
      printk(" OK\n");
    } else {
      printk(" MISMATCH!\n");
      kpanic(".text section checksum mismatch after init - code corruption "
             "detected");
      all_ok = false;
    }

    printk("[MEM] .rodata checksum: expected=0x");
    printk_hex32(expected_rodata_crc);
    printk(", actual=0x");
    printk_hex32(actual_rodata_crc);
    if (actual_rodata_crc == expected_rodata_crc) {
      printk(" OK\n");
    } else {
      printk(" MISMATCH!\n");
      kpanic(".rodata section checksum mismatch after init - data corruption "
             "detected");
      all_ok = false;
    }
  }

  if (all_ok) {
    printk("[MEM] === Post-Init Validation PASSED ===\n\n");
  } else {
    printk("[MEM] === Post-Init Validation FAILED ===\n\n");
  }
}

// Print RV64 memory layout
void platform_mem_print_layout(void) {
  printk("\n[MEM] === RISC-V 64 Memory Map (QEMU virt) ===\n");

  // DTB region (from constants)
  printk("  DTB region:          0x");
  printk_hex64(DTB_BASE);
  printk(" - 0x");
  printk_hex64(DTB_BASE + DTB_SIZE - 1);
  printk(" (");
  printk_dec((uint32_t)(DTB_SIZE / (1024 * 1024)));
  printk(" MiB reserved)\n");

  // Kernel sections (from linker symbols)
  uintptr_t text_start = (uintptr_t)_text_start;
  uintptr_t text_end = (uintptr_t)_text_end;
  uintptr_t rodata_start = (uintptr_t)_rodata_start;
  uintptr_t rodata_end = (uintptr_t)_rodata_end;
  uintptr_t bss_start = (uintptr_t)_bss_start;
  uintptr_t kernel_end = (uintptr_t)_end;

  printk("  Kernel .text:        0x");
  printk_hex64(text_start);
  printk(" - 0x");
  printk_hex64(text_end);
  printk(" (");
  printk_dec((text_end - text_start) / 1024);
  printk(" KiB)\n");

  printk("  Kernel .rodata:      0x");
  printk_hex64(rodata_start);
  printk(" - 0x");
  printk_hex64(rodata_end);
  printk(" (");
  printk_dec((rodata_end - rodata_start) / 1024);
  printk(" KiB)\n");

  printk("  Kernel .bss+stack:   0x");
  printk_hex64(bss_start);
  printk(" - 0x");
  printk_hex64(kernel_end);
  printk(" (");
  printk_dec((kernel_end - bss_start) / 1024);
  printk(" KiB)\n");

  printk("  Free RAM:            0x");
  printk_hex64(kernel_end);
  printk(" - 0x");
  printk_hex64(RAM_BASE + RAM_SIZE);
  printk(" (~");
  printk_dec((RAM_BASE + RAM_SIZE - kernel_end) / (1024 * 1024));
  printk(" MiB)\n");

  // MMIO regions (fixed addresses for QEMU virt machine)
  printk("  CLINT (Timer):       0x02000000 - 0x0200FFFF\n");
  printk("  PLIC (Interrupt):    0x0C000000 - 0x0FFFFFFF\n");
  printk("  UART (NS16550):      0x10000000 - 0x10000FFF\n");
  printk("  VirtIO MMIO:         0x10001000+ (devices at 0x1000 intervals)\n");
  printk("  PCI ECAM:            0x30000000+ (if USE_PCI=1)\n");
  printk("\n");
}

// Dump virtual address translation (RISC-V SV39/SV48)
void platform_mem_dump_translation(uintptr_t vaddr) {
  printk("\n[MEM] RV64 Virtual address translation for 0x");
  printk_hex64(vaddr);
  printk(":\n");

  // SV39: 3-level paging (most common)
  // satp → L2 → L1 → L0 → Physical
  // Each level uses 9 bits, 12-bit offset

  printk("  Address breakdown (assuming SV39):\n");
  uint32_t vpn2 = (vaddr >> 30) & 0x1FF; // VPN[2]
  uint32_t vpn1 = (vaddr >> 21) & 0x1FF; // VPN[1]
  uint32_t vpn0 = (vaddr >> 12) & 0x1FF; // VPN[0]
  uint32_t offset = vaddr & 0xFFF;

  printk("  VPN[2]: 0x");
  printk_hex32(vpn2);
  printk(", VPN[1]: 0x");
  printk_hex32(vpn1);
  printk(", VPN[0]: 0x");
  printk_hex32(vpn0);
  printk(", Offset: 0x");
  printk_hex32(offset);
  printk("\n");

  printk("  (Full page table walk requires satp register access)\n");
}

// Dump RV64 page tables
void platform_mem_dump_pagetables(void) {
  printk("\n[MEM] RV64 Page Table Dump:\n");
  printk("  RV64 page table dumping not implemented\n");
  printk("  (Requires reading satp CSR and walking page tables)\n\n");
}

// Set memory guard (canary value)
void platform_mem_set_guard(void *addr, uint32_t size) {
  // Generic implementation - write pattern at boundaries
  if (size >= 8) {
    *(uint64_t *)addr = 0xDEADBEEFCAFEBABEULL;
    if (size >= 16) {
      *(uint64_t *)((uintptr_t)addr + size - 8) = 0xDEADBEEFCAFEBABEULL;
    }
  }
}

// Check memory guard is intact
bool platform_mem_check_guard(void *addr, uint32_t size) {
  bool ok = true;

  if (size >= 8) {
    uint64_t val = *(uint64_t *)addr;
    if (val != 0xDEADBEEFCAFEBABEULL) {
      printk("[MEM] GUARD FAILED at 0x");
      printk_hex64((uintptr_t)addr);
      printk(": expected 0xDEADBEEFCAFEBABE, got 0x");
      printk_hex64(val);
      printk("\n");
      kpanic("Memory guard corruption detected");
      ok = false;
    }

    if (size >= 16) {
      val = *(uint64_t *)((uintptr_t)addr + size - 8);
      if (val != 0xDEADBEEFCAFEBABEULL) {
        printk("[MEM] GUARD FAILED at 0x");
        printk_hex64((uintptr_t)addr + size - 8);
        printk(": expected 0xDEADBEEFCAFEBABE, got 0x");
        printk_hex64(val);
        printk("\n");
        kpanic("Memory guard corruption detected");
        ok = false;
      }
    }
  }

  return ok;
}

#endif // KDEBUG
