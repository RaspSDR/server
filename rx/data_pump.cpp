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

#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "valgrind.h"
#include "rx.h"
#include "misc.h"
#include "timer.h"
#include "web.h"
#include "coroutines.h"
#include "debug.h"
#include "data_pump.h"
#include "fpga.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <math.h>

rx_dpump_t rx_dpump[MAX_RX_CHANS];
dpump_t dpump;

bool have_snd_users;
static rx_shmem_t rx_shmem;
rx_shmem_t *rx_shmem_p = &rx_shmem;

// rescale factor from hardware samples to what CuteSDR code is expecting
const TYPEREAL rescale = MPOW(2, -RXOUT_SCALE + CUTESDR_SCALE);
static u4_t last_run_us;

#ifdef SND_SEQ_CHECK
	static bool initial_seq;
	static u2_t snd_seq;
#endif


static void snd_service()
{
    u4_t diff, moved = 0;

    evLatency(EC_EVENT, EV_DPUMP, 0, "DATAPUMP", "snd_service() BEGIN");
    do
    {
        TYPECPX *i_samps[MAX_RX_CHANS];
        for (int ch = 0; ch < rx_chans; ch++)
        {
            rx_dpump_t *rx = &rx_dpump[ch];
            i_samps[ch] = rx->in_samps[rx->wr_pos];
        }

        for (int i = 0; i < nrx_samps;i++)
        {
            s4_t data[MAX_RX_CHANS * 2];
            memcpy(data, (void*)fpga_rx_data, sizeof(s4_t) * 2 * rx_chans);
            for (int ch = 0; ch < rx_chans; ch++)
            {
                s4_t i, q;
                i = data[ch * 2];
                q = data[ch * 2 + 1];

                // NB: I/Q reversed to get correct sideband polarity; fixme: why?
                // [probably because mixer NCO polarity is wrong, i.e. cos/sin should really be cos/-sin]
                i_samps[ch]->re = i * rescale + DC_offset_I;
                i_samps[ch]->im = q * rescale + DC_offset_Q;
                i_samps[ch]++;
            }

            NextTaskFast("RX Read");
        }

        // move wr_pos to inform reader to consume
        for (int ch = 0; ch < rx_chans; ch++)
        {
            if (rx_channels[ch].data_enabled)
            {
                rx_dpump_t *rx = &rx_dpump[ch];

                rx->ticks[rx->wr_pos] = 0;

#ifdef SND_SEQ_CHECK
                rx->in_seq[rx->wr_pos] = snd_seq;
#endif

                rx->wr_pos = (rx->wr_pos + 1) & (N_DPBUF - 1);

                diff = (rx->wr_pos >= rx->rd_pos) ? rx->wr_pos - rx->rd_pos : N_DPBUF - rx->rd_pos + rx->wr_pos;
                dpump.in_hist[diff]++;

// #define DATA_PUMP_DEBUG
#ifdef DATA_PUMP_DEBUG
                if (rx->wr_pos == rx->rd_pos)
                {
                    real_printf("#%d ", ch);
                    fflush(stdout);
                }
#endif
            }
        }

        diff = 0;

        evLatency(EC_EVENT, EV_DPUMP, 0, "DATAPUMP", evprintf("MOVED %d diff %d sto %d cur %d %.3f msec", moved, diff, stored, current, (timer_us() - last_run_us) / 1e3));

        if (dpump.force_reset)
        {
            if (!dpump.force_reset)
                dpump.resets++;
            dpump.force_reset = false;

// dump on excessive latency between runs
#ifdef EV_MEAS_DPUMP_LATENCY
            // if (ev_dump /*&& dpump.resets > 1*/) {
            u4_t last = timer_us() - last_run_us;
            if ((ev_dump || bg) && last_run_us != 0 && last >= 40000)
            {
                evLatency(EC_EVENT, EV_DPUMP, 0, "DATAPUMP", evprintf("latency %.3f msec", last / 1e3));
                evLatency(EC_DUMP, EV_DPUMP, ev_dump, "DATAPUMP", evprintf("DUMP in %.3f sec", ev_dump / 1000.0));
            }
#endif

#if 0
#ifndef USE_VALGRIND
                    lprintf("DATAPUMP RESET #%d %5d %5d %5d %.3f msec\n",
                        dpump.resets, diff, stored, current, (timer_us() - last_run_us)/1e3);
#endif
#endif

            memset(dpump.hist, 0, sizeof(dpump.hist));
            memset(dpump.in_hist, 0, sizeof(dpump.in_hist));
            diff = 0;
        }
        else
        {
            dpump.hist[diff]++;
#if 0
                if (ev_dump && p1 && p2 && dpump.hist[p1] > p2) {
                    printf("DATAPUMP DUMP %d %d %d\n", diff, stored, current);
                    evLatency(EC_DUMP, EV_DPUMP, ev_dump, ">diff",
                        evprintf("MOVED %d, diff %d sto %d cur %d, DUMP", moved, diff, stored, current));
                }
#endif
        }

        last_run_us = timer_us();

    } while (diff > 1);
    evLatency(EC_EVENT, EV_DPUMP, 0, "DATAPUMP", evprintf("MOVED %d", moved));
}

static void data_pump(void *param)
{
	evDP(EC_EVENT, EV_DPUMP, -1, "dpump_init", evprintf("INIT: SPI CTRL_SND_INTR %d",
		GPIO_READ_BIT(SND_INTR)));

	while (1) {

		evDP(EC_EVENT, EV_DPUMP, -1, "data_pump", evprintf("SLEEPING: SPI CTRL_SND_INTR %d",
			GPIO_READ_BIT(SND_INTR)));

		//#define MEAS_DATA_PUMP
		#ifdef MEAS_DATA_PUMP
		    u4_t quanta = FROM_VOID_PARAM(TaskSleepReason("wait for interrupt"));
            static u4_t last, cps, max_quanta, sum_quanta;
            u4_t now = timer_sec();
            if (last != now) {
                for (; last < now; last++) {
                    if (last < (now-1))
                        real_printf("d- ");
                    else
                        real_printf("d%d|%d/%d ", cps, sum_quanta/(cps? cps:1), max_quanta);
                    fflush(stdout);
                }
                max_quanta = sum_quanta = 0;
                cps = 0;
            } else {
                if (quanta > max_quanta) max_quanta = quanta;
                sum_quanta += quanta;
                cps++;
            }
        #else
            //TaskSleepReason("wait for interrupt");
            while (fpga_status->rx_fifo < nrx_samps * 2 * rx_chans) {
                TaskSleepUsec(100);
            }
        #endif

		evDP(EC_EVENT, EV_DPUMP, -1, "data_pump", evprintf("WAKEUP: SPI CTRL_SND_INTR %d",
			GPIO_READ_BIT(SND_INTR)));

		snd_service();
		
		for (int ch=0; ch < rx_chans; ch++) {
			rx_chan_t *rx = &rx_channels[ch];
			if (!rx->chan_enabled) continue;
			conn_t *c = rx->conn;
			assert(c != NULL);
			assert(c->type == STREAM_SOUND);
			if (c->task) {
				TaskWakeup(c->task);
			}
		}
	}
}

void data_pump_start_stop()
{
	bool no_users = true;
	for (int i = 0; i < rx_chans; i++) {
        rx_chan_t *rx = &rx_channels[i];
		if (rx->chan_enabled) {
			no_users = false;
			break;
		}
	}
	
	have_snd_users = !no_users;
}

void data_pump_init()
{
	//printf("data pump: rescale=%.6g\n", rescale);

	CreateTaskF(data_pump, 0, DATAPUMP_PRIORITY, 0);
}
