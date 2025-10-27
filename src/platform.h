// Platform abstraction layer
#pragma once

// Each platform implements platform_impl.h with platform-specific features
#include "platform_impl.h"

void fdt_dump(void* fdt);
void platform_timer_init(void);

