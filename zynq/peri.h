#pragma once

#include <sys/wait.h>
#include "fpga.h"

void peri_init();
void peri_free();

#define FPGA_ID_RX14_WF4 4

#define GPIO_READ_BIT(x) (fpga_status->rx_fifo >= 512)

void rf_enable_airband(bool enabled);
void rf_attn_set(float attn_dB);