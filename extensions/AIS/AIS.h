#pragma once

#include "types.h"
#include "kiwi.h"
#include "coroutines.h"

#define AIS_MAX_FRAME_BITS 2048

typedef struct {
    int phase, sum, previous_level, shift, raw_count;
    bool have_level, in_frame;
    float dc;
    u1_t raw[AIS_MAX_FRAME_BITS];
} ais_decoder_t;

typedef struct {
    int rx_chan;
    bool running;
    tid_t tid;
    int rd_pos;
    bool seq_init;
    u4_t seq;
    double tuned_f;
    ais_decoder_t decoder[2];
} ais_chan_t;
