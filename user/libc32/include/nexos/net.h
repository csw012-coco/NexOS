#pragma once

#include "user/public/sysapi.h"

#define NEX_QUERY_RTL8139 SYS_QUERY_RTL8139

int rtl8139_query(struct syscall_rtl8139_info *info);
int rtl8139_tx_test(void);
int rtl8139_rx_dump(struct syscall_rtl8139_rx_info *info);
int rtl8139_tx_send(const void *data, uint32_t bytes);
