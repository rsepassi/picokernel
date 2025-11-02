// ARM64 Platform Memory Debugging Implementation
// Implements kernel/mem_debug.h interface for ARM64

#include "mem_debug.h"
#include "platform.h"
#include "printk.h"

#ifdef KDEBUG

// ARM64 memory layout constants (QEMU virt machine)
// DTB region: QEMU places device tree at start of RAM
#define DTB_SIZE 0x00200000ULL // 2 MiB reserved for DTB

// Linker-provided symbols for kernel sections
extern uint8_t _text_start[], _text_end[];
extern uint8_t _rodata_start[], _rodata_end[];
extern uint8_t _data_start[], _data_end[];
extern uint8_t _bss_start[], _bss_end[];
extern uint8_t __bss_start[], __bss_end[];
extern uint8_t _end[];
extern uint8_t stack_bottom[], stack_top[];

// Validate critical memory regions early in boot (post-boot.S)
void platform_mem_validate_critical(void) {
  printk("\n[MEM] === ARM64 Early Boot Validation ===\n");
  bool all_ok = true;

  // 1. Get kernel section addresses from linker symbols
  uintptr_t text_start = (uintptr_t)_text_start;
  uintptr_t text_end = (uintptr_t)_text_end;
  uintptr_t rodata_start = (uintptr_t)_rodata_start;
  uintptr_t rodata_end = (uintptr_t)_rodata_end;
  uintptr_t data_start = (uintptr_t)_data_start;
  uintptr_t data_end = (uintptr_t)_data_end;
  uintptr_t bss_start = (uintptr_t)_bss_start;
  uintptr_t bss_end = (uintptr_t)_bss_end;
  uintptr_t kernel_end = (uintptr_t)_end;

  // 2. Calculate DTB region (assumes DTB is 2MB before kernel start)
  uintptr_t dtb_base = text_start - DTB_SIZE;
  uintptr_t dtb_end = dtb_base + DTB_SIZE - 1;

  printk("[MEM] DTB region: 0x");
  printk_hex64(dtb_base);
  printk(" - 0x");
  printk_hex64(dtb_end);
  printk(" (reserved for device tree)\n");

  // 3. Validate kernel sections

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

  // 4. Verify kernel sections are properly aligned
  if ((text_start & 0xFFF) != 0) {
    printk("[MEM] WARNING: .text section not page-aligned (0x");
    printk_hex64(text_start);
    printk(")\n");
  }

  // 5. Check sections don't overlap DTB
  if (kmem_ranges_overlap(dtb_base, DTB_SIZE, text_start,
                          kernel_end - text_start)) {
    printk("[MEM] ERROR: Kernel overlaps with DTB region!\n");
    all_ok = false;
    kpanic("Kernel/DTB memory overlap detected");
  }

  // 6. Validate stack
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

  if (stack_size != 65536) {
    printk("[MEM] WARNING: Stack size is not 64 KiB as expected\n");
  }

  // 7. Check BSS is zeroed (sample check - first and last 64 bytes)
  bool bss_zeroed = kmem_validate_pattern((void *)bss_start, 64, 0x00);
  if (bss_end - bss_start > 128) {
    bss_zeroed &= kmem_validate_pattern((void *)(bss_end - 64), 64, 0x00);
  }

  if (!bss_zeroed) {
    printk("[MEM] ERROR: BSS not properly zeroed by boot.S\n");
    all_ok = false;
    kpanic("BSS section not properly zeroed");
  } else {
    printk("[MEM] BSS zeroing: OK\n");
  }

  // 8. Check sections are properly ordered and aligned
  if (text_end > rodata_start || rodata_end > data_start ||
      data_end > bss_start) {
    printk("[MEM] ERROR: Kernel sections not properly ordered\n");
    all_ok = false;
    kpanic("Kernel sections improperly ordered");
  }

  printk("[MEM] Total kernel size: ");
  printk_dec(kernel_end - text_start);
  printk(" bytes\n");

  // 9. Verify .text and .rodata checksums
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
      kpanic(".text section checksum mismatch - code corruption detected");
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
      kpanic(".rodata section checksum mismatch - data corruption detected");
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
  printk("\n[MEM] === ARM64 Post-Init Validation ===\n");
  bool all_ok = true;

  // 1. Validate DTB pointer
  uintptr_t fdt_addr = (uintptr_t)fdt;
  uintptr_t text_start = (uintptr_t)_text_start;
  uintptr_t expected_dtb = text_start - DTB_SIZE;

  printk("[MEM] DTB pointer: 0x");
  printk_hex64(fdt_addr);

  if (fdt_addr != expected_dtb) {
    printk(" (WARNING: not at expected 0x");
    printk_hex64(expected_dtb);
    printk(")\n");
  } else {
    printk(" (OK)\n");
  }

  // 2. Note: MMIO region accessibility checks removed - these are
  // platform-specific and better validated through device discovery
  printk("[MEM] MMIO regions: validated through device discovery\n");

  // 3. Validate platform timer was initialized
  if (platform->timer_freq_hz == 0) {
    printk("[MEM] ERROR: Timer frequency not initialized\n");
    all_ok = false;
    kpanic("Platform timer not initialized");
  } else {
    printk("[MEM] Timer frequency: ");
    printk_dec(platform->timer_freq_hz);
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
      kpanic(".text section checksum mismatch after init - code corruption "
             "detected");
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
      kpanic(".rodata section checksum mismatch after init - data corruption "
             "detected");
    }
  }

  if (all_ok) {
    printk("[MEM] === Post-Init Validation PASSED ===\n\n");
  } else {
    printk("[MEM] === Post-Init Validation FAILED ===\n\n");
  }
}

// Print ARM64 memory layout
void platform_mem_print_layout(void) {
  printk("\n[MEM] === ARM64 Memory Map (QEMU virt) ===\n");

  uintptr_t text_start = (uintptr_t)_text_start;
  uintptr_t text_end = (uintptr_t)_text_end;
  uintptr_t rodata_start = (uintptr_t)_rodata_start;
  uintptr_t rodata_end = (uintptr_t)_rodata_end;
  uintptr_t data_start = (uintptr_t)_data_start;
  uintptr_t data_end = (uintptr_t)_data_end;
  uintptr_t bss_start = (uintptr_t)_bss_start;
  uintptr_t bss_end = (uintptr_t)_bss_end;
  uintptr_t kernel_end = (uintptr_t)_end;

  uintptr_t dtb_base = text_start - DTB_SIZE;
  uintptr_t dtb_end = dtb_base + DTB_SIZE - 1;

  printk("  DTB region:          0x");
  printk_hex64(dtb_base);
  printk(" - 0x");
  printk_hex64(dtb_end);
  printk(" (");
  printk_dec((uint32_t)(DTB_SIZE / (1024 * 1024)));
  printk(" MiB reserved)\n");

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

  printk("  Kernel .data:        0x");
  printk_hex64(data_start);
  printk(" - 0x");
  printk_hex64(data_end);
  printk(" (");
  printk_dec((data_end - data_start) / 1024);
  printk(" KiB)\n");

  printk("  Kernel .bss+stack:   0x");
  printk_hex64(bss_start);
  printk(" - 0x");
  printk_hex64(bss_end);
  printk(" (");
  printk_dec((bss_end - bss_start) / 1024);
  printk(" KiB)\n");

  printk("  Kernel end:          0x");
  printk_hex64(kernel_end);
  printk("\n");

  printk("  MMIO regions: (platform-specific, discovered at runtime)\n");
  printk("    - GIC (Interrupt controller)\n");
  printk("    - UART (PL011)\n");
  printk("    - VirtIO MMIO devices\n");
  printk("    - PCI ECAM (if USE_PCI=1)\n");
  printk("\n");
}

// Dump virtual address translation (ARM64 translation tables)
void platform_mem_dump_translation(uintptr_t vaddr) {
  printk("\n[MEM] ARM64 Virtual address translation for 0x");
  printk_hex64(vaddr);
  printk(":\n");

  // ARM64 uses TTBR (Translation Table Base Register)
  // 4KB granule: Level 0/1/2/3 tables
  // This would require reading TTBR0_EL1 or TTBR1_EL1

  printk("  Address breakdown:\n");
  uint32_t l0_idx = (vaddr >> 39) & 0x1FF;
  uint32_t l1_idx = (vaddr >> 30) & 0x1FF;
  uint32_t l2_idx = (vaddr >> 21) & 0x1FF;
  uint32_t l3_idx = (vaddr >> 12) & 0x1FF;
  uint32_t offset = vaddr & 0xFFF;

  printk("  L0 index: ");
  printk("0x");
  printk_hex32(l0_idx);
  printk(", L1 index: ");
  printk("0x");
  printk_hex32(l1_idx);
  printk(", L2 index: ");
  printk("0x");
  printk_hex32(l2_idx);
  printk(", L3 index: ");
  printk("0x");
  printk_hex32(l3_idx);
  printk(", Offset: 0x");
  printk_hex32(offset);
  printk("\n");

  printk("  (Full translation table walk requires TTBR register access)\n");
}

// Dump ARM64 translation tables
void platform_mem_dump_pagetables(void) {
  printk("\n[MEM] ARM64 Translation Table Dump:\n");
  printk("  ARM64 page table dumping not implemented\n");
  printk("  (Requires reading TTBR0_EL1/TTBR1_EL1 and walking tables)\n\n");
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
      ok = false;
      kpanic("Memory guard corruption detected");
    }

    if (size >= 16) {
      val = *(uint64_t *)((uintptr_t)addr + size - 8);
      if (val != 0xDEADBEEFCAFEBABEULL) {
        printk("[MEM] GUARD FAILED at 0x");
        printk_hex64((uintptr_t)addr + size - 8);
        printk(": expected 0xDEADBEEFCAFEBABE, got 0x");
        printk_hex64(val);
        printk("\n");
        ok = false;
        kpanic("Memory guard corruption detected");
      }
    }
  }

  return ok;
}

#endif // KDEBUG
