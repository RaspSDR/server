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

inline void ctrl_clr_set(u2_t clr, u2_t set){}

u64_t fpga_dna();

#define RESET_RX (1 << 0)
#define RESET_WF0 (1 << 1)
#define RESET_WF1 (1 << 2)
#define RESET_WF2 (1 << 3)
#define RESET_WF3 (1 << 4)
#define RESET_PPS (1 << 8)

typedef struct {
    uint32_t reset;
    uint32_t rx_freq[16];
    struct {
        uint32_t wf_freq;
        uint32_t wf_decim;
    }wf_config[4];
    uint8_t antenna;
}__attribute__((packed)) FPGA_Config;
static_assert(sizeof(FPGA_Config) == 808/8);

typedef struct {
    uint32_t signature;
    uint32_t rx_fifo;
    uint32_t wf_fifo[4];
    uint32_t pps_fifo;
    uint64_t fpga_dna;
    uint64_t tsc;
}__attribute__((packed)) FPGA_Status;
static_assert(sizeof(FPGA_Status) == 352/8);

extern const volatile int32_t *fpga_rx_data;
extern const volatile uint32_t *fpga_wf_data[4];
extern const volatile uint32_t *fpga_pps_data;
extern volatile FPGA_Config *fpga_config;
extern const volatile FPGA_Status *fpga_status;
