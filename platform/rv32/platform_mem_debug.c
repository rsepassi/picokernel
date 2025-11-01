// RISC-V 32 Platform Memory Debugging Implementation
// Implements kernel/mem_debug.h interface for RV32

#include "mem_debug.h"
#include "platform.h"
#include "printk.h"

#ifdef KDEBUG

// RISC-V 32 memory layout constants (QEMU virt machine)
#define DTB_BASE 0x80000000UL
#define DTB_SIZE 0x00200000UL // 2 MiB reserved
#define KERNEL_BASE 0x80200000UL
#define RAM_BASE 0x80000000UL
#define RAM_SIZE 0x08000000UL // 128 MiB default

// Linker-provided symbols
extern uint8_t _text_start[], _text_end[];
extern uint8_t _rodata_start[], _rodata_end[];
extern uint8_t _data_start[], _data_end[];
extern uint8_t _bss_start[], _bss_end[];
extern uint8_t _end[];
extern uint8_t stack_bottom[], stack_top[];

// Validate critical memory regions early in boot (post-boot.S)
void platform_mem_validate_critical(void) {
  printk("\n[MEM] === RISC-V 32 Early Boot Validation ===\n");
  bool all_ok = true;

  // 1. Validate DTB region
  printk("[MEM] DTB region: 0x");
  printk_hex32(DTB_BASE);
  printk(" - 0x");
  printk_hex32(DTB_BASE + DTB_SIZE - 1);
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
  printk_hex32(text_start);
  printk(" - 0x");
  printk_hex32(text_end);
  uint32_t text_size = text_end - text_start;
  printk(" (");
  printk_dec(text_size);
  printk(" bytes)\n");

  printk("[MEM] .rodata: 0x");
  printk_hex32(rodata_start);
  printk(" - 0x");
  printk_hex32(rodata_end);
  uint32_t rodata_size = rodata_end - rodata_start;
  printk(" (");
  printk_dec(rodata_size);
  printk(" bytes)\n");

  printk("[MEM] .data:   0x");
  printk_hex32(data_start);
  printk(" - 0x");
  printk_hex32(data_end);
  uint32_t data_size = data_end - data_start;
  printk(" (");
  printk_dec(data_size);
  printk(" bytes)\n");

  printk("[MEM] .bss:    0x");
  printk_hex32(bss_start);
  printk(" - 0x");
  printk_hex32(bss_end);
  uint32_t bss_size = bss_end - bss_start;
  printk(" (");
  printk_dec(bss_size);
  printk(" bytes)\n");

  // 3. Verify kernel base address
  if (text_start != KERNEL_BASE) {
    printk("[MEM] ERROR: Kernel not at expected base 0x80200000\n");
    all_ok = false;
  }

  // 4. Check sections don't overlap DTB
  if (kmem_ranges_overlap(DTB_BASE, DTB_SIZE, text_start,
                          kernel_end - text_start)) {
    printk("[MEM] ERROR: Kernel overlaps with DTB region!\n");
    all_ok = false;
  }

  // 5. Validate stack
  uintptr_t stack_bot = (uintptr_t)stack_bottom;
  uintptr_t stack_t = (uintptr_t)stack_top;
  uint32_t stack_size = stack_t - stack_bot;

  printk("[MEM] Stack:   0x");
  printk_hex32(stack_bot);
  printk(" - 0x");
  printk_hex32(stack_t);
  printk(" (");
  printk_dec(stack_size);
  printk(" bytes, grows down)\n");

  if (stack_size != 65536) {
    printk("[MEM] WARNING: Stack size is not 64 KiB as expected\n");
  }

  // 6. Check BSS is zeroed (sample check - first and last 64 bytes)
  bool bss_zeroed = kmem_validate_pattern((void *)bss_start, 64, 0x00);
  if (bss_end - bss_start > 128) {
    bss_zeroed &= kmem_validate_pattern((void *)(bss_end - 64), 64, 0x00);
  }

  if (!bss_zeroed) {
    printk("[MEM] ERROR: BSS not properly zeroed by boot.S\n");
    all_ok = false;
  } else {
    printk("[MEM] BSS zeroing: OK\n");
  }

  // 7. Check sections are properly ordered and aligned
  if (text_end > rodata_start || rodata_end > data_start ||
      data_end > bss_start) {
    printk("[MEM] ERROR: Kernel sections not properly ordered\n");
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
  printk("\n[MEM] === RISC-V 32 Post-Init Validation ===\n");
  bool all_ok = true;

  // 1. Validate DTB pointer
  uintptr_t fdt_addr = (uintptr_t)fdt;
  printk("[MEM] DTB pointer: 0x");
  printk_hex32(fdt_addr);

  if (fdt_addr != DTB_BASE) {
    printk(" (WARNING: not at expected 0x80000000)\n");
  } else {
    printk(" (OK)\n");
  }

  // 2. Validate MMIO regions are accessible (basic probe)
  volatile uint32_t *uart_base = (volatile uint32_t *)0x10000000UL;
  volatile uint32_t *plic_base = (volatile uint32_t *)0x0C000000UL;
  volatile uint32_t *virtio_base = (volatile uint32_t *)0x10001000UL;

  printk("[MEM] UART (0x10000000): ");
  uint32_t uart_val = *uart_base;
  (void)uart_val; // Mark as used
  printk("accessible\n");

  printk("[MEM] PLIC (0x0C000000): ");
  uint32_t plic_val = *plic_base;
  (void)plic_val; // Mark as used
  printk("accessible\n");

  printk("[MEM] VirtIO MMIO (0x10001000): ");
  uint32_t virtio_val = *virtio_base;
  (void)virtio_val; // Mark as used
  printk("accessible\n");

  // 3. Validate platform timer was initialized
  if (platform->timer_freq == 0) {
    printk("[MEM] ERROR: Timer frequency not initialized\n");
    all_ok = false;
  } else {
    printk("[MEM] Timer frequency: ");
    printk_dec((uint32_t)platform->timer_freq);
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
      all_ok = false;
    }
  }

  if (all_ok) {
    printk("[MEM] === Post-Init Validation PASSED ===\n\n");
  } else {
    printk("[MEM] === Post-Init Validation FAILED ===\n\n");
  }
}

// Print RV32 memory layout
void platform_mem_print_layout(void) {
  printk("\n[MEM] === RISC-V 32 Memory Map (QEMU virt) ===\n");
  printk("  DTB region:          0x80000000 - 0x801FFFFF (2 MiB reserved)\n");
  printk("  Kernel base:         0x80200000 (text, rodata, data, bss)\n");
  printk("  RAM:                 0x80000000 - 0x88000000 (128 MiB default)\n");
  printk("  CLINT (Timer):       0x02000000 - 0x02010000\n");
  printk("  PLIC (Interrupt):    0x0C000000 - 0x10000000\n");
  printk("  UART (NS16550):      0x10000000\n");
  printk("  VirtIO MMIO:         0x10001000+ (devices at 0x1000 intervals)\n");
  printk("\n");
  printk("Note: RV32 uses SV32 virtual memory (2-level paging)\n");
  printk("      satp register controls address translation\n\n");
}

// Dump virtual address translation (RISC-V SV32)
void platform_mem_dump_translation(uintptr_t vaddr) {
  printk("\n[MEM] RV32 Virtual address translation for 0x");
  printk_hex32(vaddr);
  printk(":\n");

  // SV32: 2-level paging
  // satp → L1 → L0 → Physical
  // Each level uses 10 bits, 12-bit offset

  printk("  Address breakdown (SV32):\n");
  uint32_t vpn1 = (vaddr >> 22) & 0x3FF; // VPN[1] - 10 bits
  uint32_t vpn0 = (vaddr >> 12) & 0x3FF; // VPN[0] - 10 bits
  uint32_t offset = vaddr & 0xFFF;

  printk("  VPN[1]: ");
  printk("0x");
  printk_hex32(vpn1);
  printk(", VPN[0]: ");
  printk("0x");
  printk_hex32(vpn0);
  printk(", Offset: 0x");
  printk_hex32(offset);
  printk("\n");

  printk("  (Full page table walk requires satp register access)\n");
}

// Dump RV32 page tables
void platform_mem_dump_pagetables(void) {
  printk("\n[MEM] RV32 Page Table Dump:\n");
  printk("  RV32 page table dumping not implemented\n");
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
      printk_hex32((uintptr_t)addr);
      printk(": expected 0xDEADBEEFCAFEBABE, got 0x");
      printk_hex64(val);
      printk("\n");
      ok = false;
    }

    if (size >= 16) {
      val = *(uint64_t *)((uintptr_t)addr + size - 8);
      if (val != 0xDEADBEEFCAFEBABEULL) {
        printk("[MEM] GUARD FAILED at 0x");
        printk_hex32((uintptr_t)addr + size - 8);
        printk(": expected 0xDEADBEEFCAFEBABE, got 0x");
        printk_hex64(val);
        printk("\n");
        ok = false;
      }
    }
  }

  return ok;
}

#endif // KDEBUG
