/*
--------------------------------------------------------------------------------
This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.
This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.
You should have received a copy of the GNU Library General Public
License along with this library; if not, write to the
Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
Boston, MA  02110-1301, USA.
--------------------------------------------------------------------------------
*/

// Copyright (c) 2023 John Seamons, ZL4VO/KF6VO

#pragma once

#include "types.h"

#define RESET_RX (1 << 0)
#define RESET_WF0 (1 << 1)
#define RESET_WF1 (1 << 2)
#define RESET_WF2 (1 << 3)
#define RESET_WF3 (1 << 4)
#define RESET_PPS (1 << 7)

#define GPIO_ANTENNA (1 << 0)
#define GPIO_ANTENNA0 (1 << 0)
#define GPIO_ANTENNA1 (1 << 1)
#define GPIO_ANTENNA2 (1 << 2)
#define GPIO_ANTENNA3 (1 << 3)
#define GPIO_ANTENNA4 (1 << 4)
#define GPIO_ANTENNA5 (1 << 5)

#define GPIO_DITHER (1 << 6)
#define GPIO_PGA (1 << 7)
#define GPIO_LED (1 << 8)

typedef struct {
    uint16_t reset;
    uint16_t rx_decim;
    uint64_t rx_freq[13];
    struct {
        uint64_t wf_freq;
        uint32_t wf_decim;
    } __attribute__((packed)) wf_config[2];
    uint32_t gpios;
    uint32_t adc_ovl_mask;
}__attribute__((packed)) FPGA_Config;
static_assert(sizeof(FPGA_Config) == 1120/8, "FPGA Config register is not match");

typedef struct {
    uint32_t signature;
    uint32_t rx_fifo;
    uint32_t wf_fifo[4];
    uint32_t pps_fifo;
    uint64_t fpga_dna;
    uint64_t tsc;
}__attribute__((packed)) FPGA_Status;
static_assert(sizeof(FPGA_Status) == 352/8, "FPGA Status register is not match");

extern const volatile int32_t *fpga_rx_data;
extern const volatile uint32_t *fpga_wf_data[4];
extern const volatile uint32_t *fpga_pps_data;
extern volatile FPGA_Config *fpga_config;
extern const volatile FPGA_Status *fpga_status;

extern u64_t fpga_dna();

extern int fpga_get_wf(int rx_chan, int decimate, uint64_t freq);
extern void fpga_free_wf(int wf_chan, int rx_chan);

extern void fpga_set_antenna(int mask);
extern void fpga_set_pga(bool enabled);
extern void fpga_set_dither(bool enabled);
extern void fpga_set_led(bool enabled);

extern void fpga_rxfreq(int rx_chan, uint64_t freq);
extern void fpga_wffreq(int wf_chan, uint64_t freq);

extern void fpga_wfreset(int wf_chan);
extern void fpga_setovmask(uint32_t mask);
extern void fpga_setadclvl(uint32_t val);