// x86 Platform Memory Debugging Implementation
// Implements mem_debug.h interface for x86/x64

#include "mem_debug.h"
#include "platform.h"
#include "platform_config.h"
#include "printk.h"

#ifdef KDEBUG

// Linker-provided symbols
extern uint8_t _kernel_start[];    // Start of entire kernel image (including PVH note)
extern uint8_t _text_start[], _text_end[];
extern uint8_t _rodata_start[], _rodata_end[];
extern uint8_t _data_start[], _data_end[];
extern uint8_t _bss_start[], _bss_end[];
extern uint8_t __bss_start[], __bss_end[];
extern uint8_t _kernel_end[];      // End of kernel image
extern uint8_t _end[];             // Alias for _kernel_end
extern uint8_t stack_bottom[], stack_top[];
extern uint8_t _page_tables_start[], _page_tables_end[];
extern uint8_t _start[];           // Boot entry point

// Validate critical memory regions (page tables, kernel sections)
void platform_mem_validate_critical(void) {
  printk("\n[MEM] === x86/x64 Early Boot Validation ===\n");
  bool all_ok = true;

  // 1. Validate Page Tables region (from linker symbols)
  uintptr_t page_tables_start = (uintptr_t)_page_tables_start;
  uintptr_t page_tables_end = (uintptr_t)_page_tables_end;
  uint32_t page_tables_size = page_tables_end - page_tables_start;

  printk("[MEM] Page Tables: 0x");
  printk_hex64(page_tables_start);
  printk(" - 0x");
  printk_hex64(page_tables_end - 1);
  printk(" (");
  printk_dec(page_tables_size);
  printk(" bytes)\n");

  // 2. Validate kernel sections using linker symbols
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

  // 3. Verify kernel base address (allow for .note.Xen section before .text)
  // The linker script places kernel at _start, but .text may start slightly after
  uintptr_t kernel_base = (uintptr_t)_start;
  if (text_start < kernel_base || text_start > kernel_base + 0x1000) {
    printk("[MEM] ERROR: Kernel .text not near expected base 0x");
    printk_hex64(kernel_base);
    printk(" (at 0x");
    printk_hex64(text_start);
    printk(")\n");
    kpanic("Kernel base address corruption detected");
    all_ok = false;
  }

  // 4. Check sections don't overlap with page tables
  if (kmem_ranges_overlap(page_tables_start, page_tables_size, text_start,
                          kernel_end - text_start)) {
    printk("[MEM] ERROR: Kernel overlaps with page tables!\n");
    kpanic("Kernel/page tables memory overlap detected");
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

// Validate post-initialization state
void platform_mem_validate_post_init(platform_t *platform, void *fdt) {
  (void)fdt; // x86/x64 doesn't use FDT - uses FW_CFG instead
  printk("\n[MEM] === x86/x64 Post-Init Validation ===\n");
  bool all_ok = true;

  // 1. Validate FW_CFG device (x86 uses FW_CFG instead of FDT)
  // FW_CFG is accessed via I/O ports 0x510/0x511
  printk("[MEM] FW_CFG I/O ports: 0x510/0x511 (used for device discovery)\n");

  // 2. Probe MMIO regions are accessible
  volatile uint32_t *lapic_base = (volatile uint32_t *)platform->lapic_base;

  printk("[MEM] LAPIC (");
  if (platform->lapic_base != 0) {
    printk_hex64(platform->lapic_base);
    printk("): ");
    uint32_t lapic_id = *lapic_base;
    (void)lapic_id; // Mark as used
    printk("accessible\n");
  } else {
    printk("not initialized)\n");
    all_ok = false;
  }

  printk("[MEM] IOAPIC (");
  if (platform->ioapic.base_addr != 0) {
    printk_hex32(platform->ioapic.base_addr);
    printk("): ");
    // IOAPIC uses indirect register access, so we can't directly read
    printk("configured (max entries: ");
    printk_dec(platform->ioapic.max_entries);
    printk(")\n");
  } else {
    printk("not initialized)\n");
  }

  // Check if VirtIO MMIO devices are present (if USE_PCI=0)
  // VirtIO MMIO would typically be at known addresses, but x86 typically uses
  // PCI
  printk("[MEM] VirtIO transport: ");
#ifdef USE_PCI
  printk("PCI (ECAM or I/O port config space)\n");
#else
  printk("MMIO (not typical for x86)\n");
#endif

  // 3. Validate platform TSC frequency was initialized
  if (platform->tsc_freq == 0) {
    printk("[MEM] ERROR: TSC frequency not initialized\n");
    kpanic("Platform timer not initialized");
    all_ok = false;
  } else {
    printk("[MEM] TSC frequency: ");
    printk_dec((uint32_t)(platform->tsc_freq / 1000000)); // Convert to MHz
    printk(" MHz (");
    printk_dec((uint32_t)(platform->tsc_freq / 1000)); // Convert to KHz
    printk(" KHz)\n");
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

// Print x86 memory map
void platform_mem_print_layout(void) {
  printk("\n[MEM] === x86 Memory Map ===\n");
  printk("  BIOS/Low Memory:     0x00000000 - 0x000FFFFF (1 MiB)\n");

  uintptr_t page_tables_start = (uintptr_t)_page_tables_start;
  uintptr_t page_tables_end = (uintptr_t)_page_tables_end;
  uint32_t page_tables_size = page_tables_end - page_tables_start;

  printk("  Page Tables:         0x");
  printk_hex64(page_tables_start);
  printk(" - 0x");
  printk_hex64(page_tables_end);
  printk(" (");
  printk_dec(page_tables_size / 1024);
  printk(" KiB)\n");

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

  // Note: RAM size is not fixed in x64, but we show the typical QEMU default
  // The actual usable RAM extends to PCI_MMIO_BASE (0xC0000000 = 3GB)
  uintptr_t ram_end = PCI_MMIO_BASE;  // RAM ends where PCI MMIO begins
  printk("  Free RAM:            0x");
  printk_hex64(kernel_end);
  printk(" - 0x");
  printk_hex64(ram_end);
  printk(" (~");
  printk_dec((uint32_t)((ram_end - kernel_end) / (1024 * 1024)));
  printk(" MiB)\n");

  // Use config defines for MMIO regions
  printk("  PCI MMIO:            0x");
  printk_hex64(PCI_MMIO_BASE);
  printk(" - 0x");
  printk_hex64(PCI_MMIO_END);
  printk(" (");
  printk_dec((uint32_t)(PCI_MMIO_SIZE / (1024 * 1024)));
  printk(" MiB)\n");

  printk("  High MMIO:           0x");
  printk_hex64(HIGH_MMIO_BASE);
  printk(" - 0x");
  printk_hex64(HIGH_MMIO_END);
  printk(" (");
  printk_dec((uint32_t)(HIGH_MMIO_SIZE / (1024 * 1024)));
  printk(" MiB)\n");

  printk("    - IOAPIC:          (discovered via ACPI)\n");
  printk("    - Local APIC:      (discovered via MSR)\n");
  printk("\n");
}

// Dump virtual address translation (x86 page tables)
void platform_mem_dump_translation(uintptr_t vaddr) {
  printk("\n[MEM] Virtual address translation for 0x");
  printk_hex64(vaddr);
  printk(":\n");

  // x86-64 4-level paging: PML4 → PDPT → PD → PT → Physical
  // Cast to uint64_t to avoid shift overflow on x32 (32-bit pointers)
  uint64_t vaddr64 = vaddr;
  uint32_t pml4_idx = (vaddr64 >> 39) & 0x1FF;
  uint32_t pdpt_idx = (vaddr64 >> 30) & 0x1FF;
  uint32_t pd_idx = (vaddr64 >> 21) & 0x1FF;
  uint32_t pt_idx = (vaddr64 >> 12) & 0x1FF;
  uint32_t offset = vaddr64 & 0xFFF;

  printk("  PML4 index: ");
  printk_dec(pml4_idx);
  printk(", PDPT index: ");
  printk_dec(pdpt_idx);
  printk(", PD index: ");
  printk_dec(pd_idx);
  printk(", PT index: ");
  printk_dec(pt_idx);
  printk(", Offset: 0x");
  printk_hex32(offset);
  printk("\n");

  // Read PML4 entry (PML4 is at _page_tables_start)
  uintptr_t pml4_base = (uintptr_t)_page_tables_start;
  uintptr_t pml4_entry_addr = pml4_base + (pml4_idx * 8);
  uint64_t pml4_entry = *(uint64_t *)pml4_entry_addr;

  printk("  PML4[");
  printk_dec(pml4_idx);
  printk("] @ 0x");
  printk_hex64(pml4_entry_addr);
  printk(" = 0x");
  printk_hex64(pml4_entry);
  printk("\n");

  if (!(pml4_entry & 1)) {
    printk("  -> Not present\n");
    return;
  }

  // Continue with PDPT, PD, PT if present...
  printk("  (Full page table walk not implemented yet)\n");
}

// Dump x86 page tables
void platform_mem_dump_pagetables(void) {
  printk("\n[MEM] x86 Page Table Dump:\n");

  // Page table structure (from boot.S):
  // _page_tables_start + 0x0000: PML4 (4096 bytes) - covers 256TB
  // _page_tables_start + 0x1000: PDPT (4096 bytes) - covers 0-512GB
  // _page_tables_start + 0x2000: PD0 (4096 bytes) - maps 0-1GB with 2MB pages
  // _page_tables_start + 0x3000: PD1 (4096 bytes) - maps 1-2GB with 2MB pages
  // _page_tables_start + 0x4000: PD2 (4096 bytes) - maps 2-3GB with 2MB pages
  // _page_tables_start + 0x5000: PD3 (4096 bytes) - maps 3-4GB with 2MB pages

  uintptr_t pt_base = (uintptr_t)_page_tables_start;

  // PML4
  printk("  PML4 (0x");
  printk_hex64(pt_base);
  printk(") - First 2 entries:\n");
  kmem_dump((const void *)pt_base, 16);

  // PDPT
  printk("\n  PDPT (0x");
  printk_hex64(pt_base + 0x1000);
  printk(") - Entries 0-3:\n");
  kmem_dump((const void *)(pt_base + 0x1000), 32);

  // PD0 (first few 2MB mappings)
  printk("\n  PD0 (0x");
  printk_hex64(pt_base + 0x2000);
  printk(") - First 4 × 2MB huge page entries:\n");
  kmem_dump((const void *)(pt_base + 0x2000), 32);

  // PD3 (last GB, includes MMIO regions)
  printk("\n  PD3 (0x");
  printk_hex64(pt_base + 0x5000);
  printk(") - First 4 × 2MB huge page entries:\n");
  kmem_dump((const void *)(pt_base + 0x5000), 32);
}

// Set memory guard (canary value)
void platform_mem_set_guard(void *addr, uint32_t size) {
  // For simplicity, write a pattern at the start and end
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
