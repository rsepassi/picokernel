// ARM64 PCI Stubs
// ARM64 currently does not support PCI
// Provides stub implementations of the PCI platform interface

#pragma once

#include "platform.h"

// ARM64 implements the PCI platform interface with stubs
// These functions are never called at runtime since ARM64 only discovers MMIO
// devices
