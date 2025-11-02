#ifndef PRINTK_H
#define PRINTK_H

#include <stdint.h>

#include "printf/printf.h"

#define printk printf_

void printk_putc(char c);
void printk_hex8(uint8_t val);
void printk_hex16(uint16_t val);
void printk_hex32(uint32_t val);
void printk_hex64(uint64_t val);
uint32_t printk_dec_len(uint32_t val);
void printk_dec(uint32_t val);
void printk_ip(const uint8_t *ip);
void printk_mac(const uint8_t *mac);

#endif
