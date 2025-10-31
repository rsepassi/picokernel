#ifndef PRINTK_H
#define PRINTK_H

#include "kbase.h"

void printk(const char *str);
void printks(const char *str, size_t len);
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
