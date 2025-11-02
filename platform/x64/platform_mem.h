// x64 Platform Memory Management Internal API
// Internal functions for memory discovery and region management

#pragma once

#include "platform.h"

// Initialize memory management from PVH boot info
// Called during platform_init()
// Returns: 0 on success, negative on error
int platform_mem_init(platform_t *platform, void *pvh_info);
