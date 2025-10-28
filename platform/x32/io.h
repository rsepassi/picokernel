// x32 I/O port access functions
// Shared by uart.c and acpi.c
// (Identical to x64 version - I/O port operations don't change with pointer
// size)

#pragma once

#include <stdint.h>

// Read a byte from an I/O port
static inline uint8_t inb(uint16_t port) {
  uint8_t value;
  __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

// Write a byte to an I/O port
static inline void outb(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

// Write a word (16-bit) to an I/O port
static inline void outw(uint16_t port, uint16_t value) {
  __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}
