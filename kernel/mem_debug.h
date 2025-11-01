#ifndef MEM_DEBUG_H
#define MEM_DEBUG_H

#include "kbase.h"

#ifdef KDEBUG

// Generic memory debugging (all platforms must implement)
void platform_mem_validate_critical(void);
void platform_mem_print_layout(void);
void platform_mem_dump_translation(uptr vaddr);

// Optional (platform may provide noop)
void platform_mem_dump_pagetables(void);
void platform_mem_set_guard(void* addr, u32 size);
bool platform_mem_check_guard(void* addr, u32 size);

// Helper utilities (implemented in kernel/mem_debug.c)
void kmem_dump(const void* addr, u32 len);
void kmem_dump_range(const char* label, const void* start, const void* end);
bool kmem_validate_pattern(const void* addr, u32 len, u8 pattern);
bool kmem_ranges_overlap(uptr a_start, u32 a_size, uptr b_start, u32 b_size);

#else
// Compiled out in release builds
static inline void platform_mem_validate_critical(void) {}
static inline void platform_mem_print_layout(void) {}
static inline void platform_mem_dump_translation(uptr vaddr) { (void)vaddr; }
static inline void platform_mem_dump_pagetables(void) {}
static inline void platform_mem_set_guard(void* addr, u32 size) { (void)addr; (void)size; }
static inline bool platform_mem_check_guard(void* addr, u32 size) { (void)addr; (void)size; return true; }
static inline void kmem_dump(const void* addr, u32 len) { (void)addr; (void)len; }
static inline void kmem_dump_range(const char* label, const void* start, const void* end) { (void)label; (void)start; (void)end; }
static inline bool kmem_validate_pattern(const void* addr, u32 len, u8 pattern) { (void)addr; (void)len; (void)pattern; return true; }
static inline bool kmem_ranges_overlap(uptr a_start, u32 a_size, uptr b_start, u32 b_size) { (void)a_start; (void)a_size; (void)b_start; (void)b_size; return false; }
#endif

#endif // MEM_DEBUG_H
