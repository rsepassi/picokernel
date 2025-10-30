#include "printk.h"

#include "platform.h"

void printk_putc(char c) { platform_uart_putc(c); }

void printk(const char *str) {
  size_t len = 0;
  while (str[len]) {
    len++;
  }
  printks(str, len);
}

void printks(const char *str, size_t len) {
  for (size_t i = 0; i < len; i++) {
    printk_putc(str[i]);
  }
}

void printk_hex8(uint8_t val) {
  const char hex[] = "0123456789abcdef";
  printk("0x");
  printk_putc(hex[(val >> 4) & 0xf]);
  printk_putc(hex[val & 0xf]);
}

void printk_hex16(uint16_t val) {
  const char hex[] = "0123456789abcdef";
  printk("0x");
  for (int i = 12; i >= 0; i -= 4) {
    printk_putc(hex[(val >> i) & 0xf]);
  }
}

void printk_hex32(uint32_t val) {
  const char hex[] = "0123456789abcdef";
  printk("0x");
  for (int i = 28; i >= 0; i -= 4) {
    printk_putc(hex[(val >> i) & 0xf]);
  }
}

void printk_hex64(uint64_t val) {
  const char hex[] = "0123456789abcdef";
  printk("0x");
  for (int i = 60; i >= 0; i -= 4) {
    printk_putc(hex[(val >> i) & 0xf]);
  }
}

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

void printk_dec(uint32_t val) {
  if (val == 0) {
    printk_putc('0');
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
    printk_putc(buf[--i]);
  }
}

void printk_ip(const uint8_t *ip) {
  printk_dec(ip[0]);
  printk(".");
  printk_dec(ip[1]);
  printk(".");
  printk_dec(ip[2]);
  printk(".");
  printk_dec(ip[3]);
}

void printk_mac(const uint8_t *mac) {
  const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 6; i++) {
    if (i > 0) {
      printk_putc(':');
    }
    printk_putc(hex[(mac[i] >> 4) & 0xf]);
    printk_putc(hex[mac[i] & 0xf]);
  }
}
