#ifndef RUSH_SHA256_H
#define RUSH_SHA256_H

#include <stddef.h>

/* Computes SHA-256 of `data` (length `len`) and writes the result as
 * 64 lowercase hex characters + a null terminator into `out_hex`,
 * which must be at least 65 bytes. */
void sha256_hex(const unsigned char *data, size_t len, char out_hex[65]);

#endif
