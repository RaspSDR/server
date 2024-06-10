#pragma once

#include <sys/wait.h>
#include "fpga.h"

void peri_init();
void peri_free();

void rf_enable_airband(bool enabled);
void rf_attn_set(float attn_dB);

void sd_enable(bool write);
void clock_correction(float freq_error);