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

// Copyright (c) 2015 John Seamons, ZL4VO/KF6VO

#pragma once

#include "types.h"
#include "kiwi.gen.h"
#include "str.h"
#include "printf.h"
#include "datatypes.h"
#include "coroutines.h"
#include "cfg.h"
#include "non_block.h"

#define I   0
#define Q   1
#define NIQ 2

// The hardware returns RXO_BITS (typically 32-bits) and scaling down by RXOUT_SCALE
// will convert this to a +/- 1.0 float.
// But the CuteSDR code assumes a scaling of +/- 32.0k, so we scale up by CUTESDR_SCALE.
#define RXOUT_SCALE     (RXO_BITS - 1) // s32 -> +/- 1.0
#define CUTESDR_SCALE   15             // +/- 1.0 -> +/- 32.0k (s16 equivalent)
#define CUTESDR_MAX_VAL ((float)((1 << CUTESDR_SCALE) - 1))
#define CUTESDR_MAX_PWR (CUTESDR_MAX_VAL * CUTESDR_MAX_VAL)

typedef enum { PLATFORM_ZYNQ7010 = 0 } platform_e;
const char* const platform_s[] = { "zynq7010" };

#define RF_ATTN_ALLOW_EVERYONE               0
#define RF_ATTN_ALLOW_LOCAL_ONLY             1
#define RF_ATTN_ALLOW_LOCAL_OR_PASSWORD_ONLY 2

typedef struct {
    platform_e platform;
    bool dbgUs;
    bool airband;

    bool ext_clk;
    bool allow_admin_conns;
    bool spectral_inversion, spectral_inversion_lockout;

    float rf_attn_dB;

    int CAT_fd, CAT_ch;

    // lat/lon returned by ipinfo lookup
    bool ipinfo_ll_valid;
    float ipinfo_lat, ipinfo_lon;

    // low-res lat/lon from timezone process
    int lowres_lat, lowres_lon;

    int ant_switch_nch;
    bool wf_share;
    bool narrowband;
    uint32_t snd_rate; // 12K, 24k, 48k, 96k
} kiwi_t;

extern kiwi_t kiwi;

extern bool background_mode, any_preempt_autorun,
    DUC_enable_start, rev_enable_start, web_nocache, force_camp,
    snr_local_time, log_local_ip, DRM_enable, have_snd_users, admin_keepalive;

extern int port, serial_number, ip_limit_mins, test_flag, n_camp,
    inactivity_timeout_mins, S_meter_cal, waterfall_cal, current_nusers, debug_v,
    utc_offset, dst_offset, reg_kiwisdr_com_status, kiwi_reg_lo_kHz, kiwi_reg_hi_kHz,
    web_caching_debug, snr_meas_interval_hrs, snr_all, snr_HF, ant_connected;

extern char** main_argv;
extern float g_genfreq, g_genampl, g_mixfreq, max_thr;
extern double ui_srate, ui_srate_kHz, freq_offset_kHz, freq_offmax_kHz;
#define freq_offset freq_offset_kHz // ant switch ext compatibility
extern TYPEREAL DC_offset_I, DC_offset_Q;
extern kstr_t* cpu_stats_buf;
extern char *tzone_id, *tzone_name;
extern cfg_t cfg_ipl;
extern lock_t spi_lock;

extern int p0, p1, p2;
extern float p_f[8];
extern int p_i[8];


typedef enum { DOM_SEL_NAM = 0,
               DOM_SEL_DUC = 1,
               DOM_SEL_PUB = 2,
               DOM_SEL_SIP = 3,
               DOM_SEL_REV = 4 } dom_sel_e;

typedef enum { RX4_WF4 = 0,
               RX8_WF2 = 1,
               RX3_WF3 = 2,
               RX14_WF0 = 3 } firmware_e;

#define KEEPALIVE_SEC         60
#define KEEPALIVE_SEC_NO_AUTH 20 // don't hang the rx channel as long if waiting for password entry

// print_stats
#define STATS_GPS      0x01
#define STATS_GPS_SOLN 0x02
#define STATS_TASK     0x04

#define IDENT_LEN_MIN 16 // e.g. "wsprdaemon_v3.0a" is 16 chars

void kiwi_restart();

void update_freqs(bool* update_cfg = NULL);
void update_vars_from_config(bool called_at_init = false);
void cfg_adm_transition();
void dump();

void c2s_sound_init();
void c2s_sound_setup(void* param);
void c2s_sound(void* param);
void c2s_sound_shutdown(void* param);

void c2s_waterfall_init();
void c2s_waterfall_compression(int rx_chan, bool compression);
void c2s_waterfall_no_sync(int rx_chan, bool no_sync);
void c2s_waterfall_setup(void* param);
void c2s_waterfall(void* param);
void c2s_waterfall_shutdown(void* param);

void c2s_mon_setup(void* param);
void c2s_mon(void* param);

void c2s_admin_setup(void* param);
void c2s_admin_shutdown(void* param);
void c2s_admin(void* param);

void c2s_mfg_setup(void* param);
void c2s_mfg(void* param);

void stat_task(void* param);
