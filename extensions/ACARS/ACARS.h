#pragma once

#include "types.h"
#include "kiwi.h"
#include "coroutines.h"

#include <sys/types.h>

typedef struct {
    int rx_chan;
    bool running;
    bool test_pending;
    tid_t feed_tid;
    tid_t decoder_tid;
    int rd_pos;
    bool seq_init;
    u4_t seq;
    pid_t pid;
    int input_pipe;
    int output_pipe;
    double tuned_f;
} acars_chan_t;
