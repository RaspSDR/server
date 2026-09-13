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

// Copyright (c) 2014-2016 John Seamons, ZL4VO/KF6VO

#pragma once

#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "web.h"

extern double adc_clock_hz;

#define ADC_CLOCK_HF  (122.88 * MHz)

enum airband_adc_clock_e {
    AIRBAND_ADC_CLOCK_98_304 = 0,
    AIRBAND_ADC_CLOCK_110_592,
    AIRBAND_ADC_CLOCK_COUNT
};

enum airband_audio_rate_mask_e {
    AIRBAND_RATE_12K = 1 << 0,
    AIRBAND_RATE_24K = 1 << 1,
    AIRBAND_RATE_36K = 1 << 2
};

typedef struct {
    const char* name;
    u4_t adc_hz;
    u4_t pll_hz;
    u1_t multisynth_div;
    u1_t audio_rate_mask;
    u4_t coverage_lo_hz;
    u4_t coverage_hi_hz;
} airband_clock_profile_t;

// ADC clk generated from FPGA via Si5351
#define ADC_CLOCK_NOM       adc_clock_hz // 66.6666 MHz 15.0 ns
#define ADC_CLOCK_TYP       adc_clock_hz // typical 20 degC value
#define ADC_CLOCK_PPM_TYP   100          // max auto adjustment we allow
#define ADC_CLOCK_PPM_LIMIT 100          // max manual adjustment we allow

// works with float or int args
// if using an int for clk_hz make it u64_t or constant ULL to prevent overflow
#define PPM_TO_HZ(clk_hz, ppm) ((clk_hz) * (ppm) / 1000000)

enum adc_clk2_corr_e {
    ADC_CLK_CORR_DISABLED = 0,
    ADC_CLK_CORR_CONTINUOUS = 1,
    ADC_CLK_CORR_EVEN_2_MIN = 2,
    ADC_CLK_CORR_5_MIN = 3,
    ADC_CLK_CORR_15_MIN = 4,
    ADC_CLK_CORR_30_MIN = 5
};

typedef struct {
    int do_corrections;
    bool is_corr;
    bool ext_ADC_clk;
    int adc_clk_corrections; // manual and GPS corrections
    int last_adc_clk_corrections;
    int adc_gps_clk_corrections; // GPS-derived corrections
    int temp_correct_offset;
    double adc_clock_base, gps_secs;
    uint32_t gpsdo_ext_clk; // ext clock output
    uint32_t clock_ref;     // reference clock freq
    int manual_adj;
    u64_t ticks; // ticks value captured at the corresponding gps_secs
    int airband_profile_requested;
    int airband_profile_effective;
    bool airband_profile_forced;
} clk_t;

extern clk_t clk;

const airband_clock_profile_t* airband_clock_profile(int profile);
bool airband_clock_supports_rate(int profile, int snd_rate_index);
int airband_clock_effective_profile(int requested_profile, int snd_rate_index);
u4_t adc_clock_nominal_hz();
double adc_clock_system();
void clock_manual_adj(int manual_adj);
void clock_init();
void clock_conn_init(conn_t* conn);
void clock_correction(double t_rx, u64_t ticks);
void tod_correction(u4_t week, int sat);
