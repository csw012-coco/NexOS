#pragma once

#include <stdint.h>

void xhci_init(void);
uint32_t xhci_port_count(void);
uint32_t xhci_connected_port_count(void);
