#include "printk.h"

#include "platform.h"

void printk(const char *str) { platform_uart_puts(str); }

void printk_putc(char c) { platform_uart_putc(c); }

void printk_hex8(uint8_t val) {
  const char hex[] = "0123456789abcdef";
  platform_uart_puts("0x");
  platform_uart_putc(hex[(val >> 4) & 0xf]);
  platform_uart_putc(hex[val & 0xf]);
}

void printk_hex16(uint16_t val) {
  const char hex[] = "0123456789abcdef";
  platform_uart_puts("0x");
  for (int i = 12; i >= 0; i -= 4) {
    platform_uart_putc(hex[(val >> i) & 0xf]);
  }
}

void printk_hex32(uint32_t val) {
  const char hex[] = "0123456789abcdef";
  platform_uart_puts("0x");
  for (int i = 28; i >= 0; i -= 4) {
    platform_uart_putc(hex[(val >> i) & 0xf]);
  }
}

void printk_hex64(uint64_t val) {
  const char hex[] = "0123456789abcdef";
  platform_uart_puts("0x");
  for (int i = 60; i >= 0; i -= 4) {
    platform_uart_putc(hex[(val >> i) & 0xf]);
  }
}

void printk_dec(uint32_t val) {
  if (val == 0) {
    platform_uart_putc('0');
    return;
  }

  char buf[12];
  int i = 0;
  while (val > 0) {
    buf[i++] = '0' + (val % 10);
    val /= 10;
  }

  /* Print in reverse order */
  while (i > 0) {
    platform_uart_putc(buf[--i]);
  }
}
