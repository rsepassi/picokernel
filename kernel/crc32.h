#ifndef CRC32_H
#define CRC32_H

#include "kbase.h"

// Compute CRC32 checksum using standard polynomial (0xEDB88320)
uint32_t crc32_compute(const void* data, uint32_t len);

#endif // CRC32_H
