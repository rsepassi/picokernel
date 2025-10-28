#ifndef PRINTK_H
#define PRINTK_H

#include "kbase.h"

void printk(const char *str);
void printk_putc(char c);
void printk_hex8(uint8_t val);
void printk_hex16(uint16_t val);
void printk_hex32(uint32_t val);
void printk_hex64(uint64_t val);
void printk_dec(uint32_t val);

#endif
