// Kernel Configuration
// Centralized configuration for vmos kernel

#pragma once

// Include platform-specific configuration
// Each platform provides kconfig_platform.h with platform-specific defines
#include "kconfig_platform.h"

// Common configuration (can be overridden by platform)

// VirtIO configuration
// Both MMIO and PCI transports are always compiled in.
// Device discovery at runtime determines which transport is used.

// Memory management settings
#ifndef KCONFIG_MAX_MEM_REGIONS
#define KCONFIG_MAX_MEM_REGIONS 16
#endif

// Future: Add other kernel-wide configuration here
// - Scheduler configuration
// - Debug flags
// - Feature toggles
