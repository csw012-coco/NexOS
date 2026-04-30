#pragma once

#include <stdint.h>

int parse_u32(const char *text, uint32_t *value);
int parse_u64(const char *text, uint64_t *value);
int parse_token(const char *text, char *out, uint32_t max_len);
