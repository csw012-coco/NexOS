#pragma once

#include "file.h"

#define NEX_READ_BLOCKING SYS_READ_BLOCKING
#define NEX_READ_NONBLOCK SYS_READ_NONBLOCK
#define NEX_READ_CHAR SYS_READ_CHAR

uint32_t write_fd(uint32_t fd, const char *data, uint32_t len);
uint32_t write_str(const char *text);
uint32_t write_err(const char *data, uint32_t len);
uint32_t write_err_str(const char *text);
