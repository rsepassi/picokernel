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

// Future: Add other kernel-wide configuration here
// - Memory management settings
// - Scheduler configuration
// - Debug flags
// - Feature toggles
