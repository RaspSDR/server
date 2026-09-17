#pragma once

#include "types.h"
#include "kiwi.h"
#include "coroutines.h"

#include <sys/types.h>

typedef struct {
    s2_t *sample_start;
    s2_t *sample_end;
    int sample_count;
} acars_t;

typedef struct {
    int rx_chan;
    bool running;
    bool test;
    tid_t feed_tid;
    tid_t decoder_tid;
    int rd_pos;
    bool seq_init;
    u4_t seq;
    pid_t pid;
    int input_pipe;
    int output_pipe;
    double tuned_f;
    s2_t *test_ptr;
} acars_chan_t;
