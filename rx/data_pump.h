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

#ifndef _DATA_PUMP_H_
#define _DATA_PUMP_H_

#include "types.h"
#include "cuteSDR.h"

#include <atomic>

#define N_DPBUF 128

typedef struct {
    std::atomic<u4_t> wr_pos, rd_pos;
    // array size really nrx_samps but made pow2 FASTFIR_OUTBUF_SIZE for indexing efficiency
    TYPECPX in_samps[N_DPBUF][FASTFIR_OUTBUF_SIZE];
    u64_t ticks[N_DPBUF];
#ifdef SND_SEQ_CHECK
    u4_t in_seq[N_DPBUF];
#endif

    TYPECPX agc_samples_c[FASTFIR_OUTBUF_SIZE];

    TYPEREAL demod_samples_r[FASTFIR_OUTBUF_SIZE];

    // real mode input buf for cw, sstv, fax decoders etc.
    std::atomic<u4_t> real_wr_pos, real_rd_pos;
    std::atomic<u4_t> real_seq;
    u4_t real_seqnum[N_DPBUF];
    TYPEMONO16 real_samples_s2[N_DPBUF][FASTFIR_OUTBUF_SIZE];

    int freqHz[N_DPBUF]; // approx freq in effect when buffer captured
} rx_dpump_t;

extern rx_dpump_t rx_dpump[MAX_RX_CHANS];

typedef struct {
    // IQ mode input buf for DRM etc.
    u4_t iq_wr_pos; // readers maintain their own private iq_rd_pos
    u4_t iq_seq, iq_seqnum[N_DPBUF];
    TYPECPX iq_samples[N_DPBUF][FASTFIR_OUTBUF_SIZE];
} iq_buf_t;

typedef struct {
    iq_buf_t iq_buf[MAX_RX_CHANS];
} rx_shmem_t;

#include "shmem_config.h"
#include "shmem.h"

#ifdef RX_SHMEM_DISABLE
extern rx_shmem_t* rx_shmem_p;
#define RX_SHMEM rx_shmem_p
#else
#define RX_SHMEM (&shmem->rx_shmem)
#endif

typedef struct {
    u4_t resets, hist[MAX_NRX_BUFS];
    bool force_reset;
    u4_t in_hist[N_DPBUF];
    int rx_adc_ovfl;
    u4_t rx_adc_ovfl_cnt;
    int audio_dropped;
} dpump_t;

extern dpump_t dpump;

void data_pump_start_stop();
void data_pump_init();

#endif
