#include "printk.h"

#include "platform.h"
#include "printf/printf.h"

void printk_putc(char c) { platform_uart_putc(c); }

void _putchar(char c) { platform_uart_putc(c); }

void printk_hex8(uint8_t val) { printf_("%02x", val); }

void printk_hex16(uint16_t val) { printf_("%04x", val); }

void printk_hex32(uint32_t val) { printf_("%08x", val); }

void printk_hex64(uint64_t val) { printf_("%016llx", val); }

uint32_t printk_dec_len(uint32_t val) {
  if (val == 0) {
    return 1;
  }

  uint32_t len = 0;
  while (val > 0) {
    len++;
    val /= 10;
  }
  return len;
}

void printk_dec(uint32_t val) { printf_("%u", val); }

void printk_ip(const uint8_t *ip) {
  printf_("%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void printk_mac(const uint8_t *mac) {
  printf_("%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
          mac[4], mac[5]);
}
