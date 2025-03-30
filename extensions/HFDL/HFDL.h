
#pragma once

#include "types.h"
#include "kiwi.h"
#include "config.h"
#include "kiwi_assert.h"
#include "mem.h"
#include "coroutines.h"
#include "data_pump.h"
#include "ext.h"	// all calls to the extension interface begin with "ext_", e.g. ext_register()
#include "web.h"

#include <sys/mman.h>

typedef struct {
	u4_t nom_rate;
    s2_t *s2p_start, *s2p_end;
    int tsamps;    
} hfdl_t;

typedef struct {
	int rx_chan;
	int run;
	bool reset, test;
	tid_t tid, dumphfdl_tid;
	int rd_pos;
	bool seq_init;
	u4_t seq;
	
    int pid;

	double freq;
    double tuned_f;
	
    TYPECPX *samps_c;
    
    s2_t *s2p, *s22p, *s2px;
    int nsamps;
    double test_f;

    int input_pipe, output_pipe;
} hfdl_chan_t;
