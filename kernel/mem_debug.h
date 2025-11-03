#ifndef MEM_DEBUG_H
#define MEM_DEBUG_H

#include "kbase.h"

#ifdef KDEBUG

// Forward declarations for platform-specific types
struct platform_t;
typedef struct platform_t platform_t;

// Generic memory debugging (all platforms must implement)
void platform_mem_validate_critical(void);
void platform_mem_validate_post_init(platform_t *platform, void *fdt);
void platform_mem_print_layout(void);
void platform_mem_dump_translation(uintptr_t vaddr);

// FDT/device tree debugging
void platform_fdt_dump(platform_t *platform, void *fdt);

// Platform-specific checksum getters (returns expected checksums)
uint32_t platform_get_expected_text_checksum(void);
uint32_t platform_get_expected_rodata_checksum(void);

// Optional (platform may provide noop)
void platform_mem_dump_pagetables(void);
void platform_mem_set_guard(void *addr, uint32_t size);
bool platform_mem_check_guard(void *addr, uint32_t size);

// Helper utilities (implemented in kernel/mem_debug.c)
void kmem_dump(const void *addr, uint32_t len);
void kmem_dump_range(const char *label, const void *start, const void *end);
bool kmem_validate_pattern(const void *addr, uint32_t len, uint8_t pattern);
bool kmem_ranges_overlap(uintptr_t a_start, uint32_t a_size, uintptr_t b_start,
                         uint32_t b_size);

// Checksumming utilities
uint32_t kmem_crc32(const void *data, uint32_t len);
uint32_t kmem_checksum_section(const void *start, const void *end);

#else
// Compiled out in release builds
struct platform_t;
typedef struct platform_t platform_t;
static inline void platform_mem_validate_critical(void) {}
static inline void platform_mem_validate_post_init(platform_t *platform,
                                                   void *fdt) {
  (void)platform;
  (void)fdt;
}
static inline void platform_mem_print_layout(void) {}
static inline void platform_mem_dump_translation(uintptr_t vaddr) {
  (void)vaddr;
}
static inline void platform_fdt_dump(platform_t *platform, void *fdt) {
  (void)platform;
  (void)fdt;
}
static inline uint32_t platform_get_expected_text_checksum(void) { return 0; }
static inline uint32_t platform_get_expected_rodata_checksum(void) { return 0; }
static inline void platform_mem_dump_pagetables(void) {}
static inline void platform_mem_set_guard(void *addr, uint32_t size) {
  (void)addr;
  (void)size;
}
static inline bool platform_mem_check_guard(void *addr, uint32_t size) {
  (void)addr;
  (void)size;
  return true;
}
static inline void kmem_dump(const void *addr, uint32_t len) {
  (void)addr;
  (void)len;
}
static inline void kmem_dump_range(const char *label, const void *start,
                                   const void *end) {
  (void)label;
  (void)start;
  (void)end;
}
static inline bool kmem_validate_pattern(const void *addr, uint32_t len,
                                         uint8_t pattern) {
  (void)addr;
  (void)len;
  (void)pattern;
  return true;
}
static inline bool kmem_ranges_overlap(uintptr_t a_start, uint32_t a_size,
                                       uintptr_t b_start, uint32_t b_size) {
  (void)a_start;
  (void)a_size;
  (void)b_start;
  (void)b_size;
  return false;
}
static inline uint32_t kmem_crc32(const void *data, uint32_t len) {
  (void)data;
  (void)len;
  return 0;
}
static inline uint32_t kmem_checksum_section(const void *start,
                                             const void *end) {
  (void)start;
  (void)end;
  return 0;
}
#endif

#endif // MEM_DEBUG_H
