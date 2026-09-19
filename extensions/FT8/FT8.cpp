// Copyright (c) 2023 John Seamons, ZL4VO/KF6VO

#include "ext.h"	// all calls to the extension interface begin with "ext_", e.g. ext_register()

#include "kiwi.h"
#include "coroutines.h"
#include "conn.h"
#include "rx_util.h"
#include "data_pump.h"
#include "mem.h"
#include "misc.h"
#include "wspr.h"
#include "FT8.h"
#include "PSKReporter.h"
#include "ft8/constants.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <strings.h>
#include <sys/mman.h>

//#define DEBUG_MSG	true
#define DEBUG_MSG	false

#define FST4W_TEST_SAMPLE_RATE 12000
#define FST4W_TEST_PERIOD      120

// rx_chan is the receiver channel number we've been assigned, 0..rx_chans
// We need this so the extension can support multiple users, each with their own ft8[] data structure.

typedef struct {
	int rx_chan;
	int run;
	int proto;
	bool debug;
	int last_freq_kHz;
	
	bool task_created;
	tid_t tid;
	int rd_pos;
	bool seq_init;
	u4_t seq;

	// autorun
	bool autorun;
	int instance;
	conn_t *arun_csnd, *arun_cext;
	double arun_dial_freq_kHz;

	bool test;
	u1_t start_test;
    int test_out_pos;
    int test_progress;
    bool test_feeding;
} ft8_t;

static ft8_t ft8[MAX_RX_CHANS];

ft8_conf_t ft8_conf;

static int ft8_arun_band[MAX_ARUN_INST];
static int ft8_arun_preempt[MAX_ARUN_INST];
static internal_conn_t iconn[MAX_ARUN_INST];

static void ft8_file_data(int rx_chan, int chan, int nsamps, TYPEMONO16 *samps, int freqHz)
{
    ft8_t *e = &ft8[rx_chan];

    if (!e->test || !e->start_test) return;
    if (!e->test_feeding) {
        e->test_feeding = true;
        ext_send_msg(rx_chan, false, "EXT test_status=feeding test_progress=0");
    }

    for (int i = 0; i < nsamps; i++) {
        int sample = (int) (((u64_t) e->test_out_pos * FST4W_TEST_SAMPLE_RATE) / snd_rate);
        if (sample >= ft8_conf.fst4w_test_samples) return;
        *samps++ = (s2_t) FLIP16(ft8_conf.fst4w_test_start[sample]);
        e->test_out_pos++;
    }

    int sample = (int) (((u64_t) e->test_out_pos * FST4W_TEST_SAMPLE_RATE) / snd_rate);
    int progress = MIN(100, sample * 100 / ft8_conf.fst4w_test_samples);
    if (progress > e->test_progress) {
        e->test_progress = progress;
        ext_send_msg(rx_chan, false, "EXT test_status=%s test_progress=%d",
            progress == 100? "decoding" : "feeding", progress);
    }
}

static void ft8_task(void *param)
{
    int rx_chan = (int) FROM_VOID_PARAM(param);
    ft8_t *e = &ft8[rx_chan];
    conn_t *conn = rx_channels[rx_chan].conn;
    rx_dpump_t *rx = &rx_dpump[rx_chan];
    decode_ft8_setup(rx_chan, e->debug);
    
	while (1) {
		TaskSleepReason("wait for wakeup");
		
		int new_freq_kHz = (int) round(conn->freqHz/1e3);
		if (e->last_freq_kHz != new_freq_kHz) {
		    //rcprintf(rx_chan, "FT8: freq changed %d => %d\n", e->last_freq_kHz, new_freq_kHz);
		    e->last_freq_kHz = new_freq_kHz;
		    decode_ft8_protocol(rx_chan, conn->freqHz, e->proto);
		}

		while (e->rd_pos != rx->real_wr_pos) {
		    if (rx->real_seqnum[e->rd_pos] != e->seq) {
                if (!e->seq_init) {
                    e->seq_init = true;
                } else {
                    u4_t got = rx->real_seqnum[e->rd_pos], expecting = e->seq;
                    rcprintf(rx_chan, "FT8 SEQ: @%d got %d expecting %d (%d)\n", e->rd_pos, got, expecting, got - expecting);
                }
                e->seq = rx->real_seqnum[e->rd_pos];
            }
            e->seq++;
		    
		    //real_printf("%d ", e->rd_pos); fflush(stdout);
		    ft8_conf.test = e->test;
		    decode_ft8_samples(rx_chan, &rx->real_samples_s2[e->rd_pos][0], FASTFIR_OUTBUF_SIZE, conn->freqHz, &e->start_test);
			e->rd_pos = (e->rd_pos+1) & (N_DPBUF-1);
		}
    }
}

void ft8_reset(ft8_t *e)
{
    memset(e, 0, sizeof(*e));
}

void ft8_close(int rx_chan)
{
    rx_util_t *r = &rx_util;
	ft8_t *e = &ft8[rx_chan];
    //rcprintf(rx_chan, "FT8: close rx_chan=%d autorun=%d arun_which=%d task_created=%d\n",
    //    rx_chan, e->autorun, r->arun_which[rx_chan], e->task_created);
    ext_unregister_receive_real_samps_task(rx_chan);

	if (e->autorun) {
        internal_conn_shutdown(&iconn[e->instance]);
	}

	if (e->task_created) {
        //rcprintf(rx_chan, "FT8: TaskRemove\n");
		TaskRemove(e->tid);
		e->task_created = false;
	}
	
	decode_ft8_free(rx_chan);
    ft8_reset(e);
    r->arun_which[rx_chan] = ARUN_NONE;
}

bool ft8_msgs(char *msg, int rx_chan)
{
	ft8_t *e = &ft8[rx_chan];
	int n;
	
	//rcprintf(rx_chan, "### ft8_msgs <%s>\n", msg);
	
	if (strcmp(msg, "SET ext_server_init") == 0) {
		e->rx_chan = rx_chan;	// remember our receiver channel number
		ext_send_msg(e->rx_chan, DEBUG_MSG, "EXT ready");
		return true;
	}

    int proto;
	if (sscanf(msg, "SET ft8_start=%d", &proto) == 1) {
        if (proto < FT8_PROTOCOL_FT8 || proto > FT8_PROTOCOL_FST4W_1800) {
            rcprintf(rx_chan, "FT8: invalid protocol %d\n", proto);
            return true;
        }
	    e->debug = kiwi.dbgUs;
	    e->proto = proto;
        conn_t *conn = rx_channels[e->rx_chan].conn;
		e->last_freq_kHz = conn->freqHz/1e3;
        ft8_conf.freq_offset_Hz = (u4_t) (freq_offset_kHz * 1e3);
		decode_ft8_init(rx_chan, proto);

		if (ft8_conf.fst4w_test_samples != 0) {
            ext_register_receive_real_samps(ft8_file_data, rx_chan);
		}

        if (!e->task_created) {
            e->tid = CreateTaskF(ft8_task, TO_VOID_PARAM(rx_chan), EXT_PRIORITY, CTF_STACK_LARGE | CTF_RX_CHANNEL | (rx_chan & CTF_CHANNEL));
            e->task_created = true;
        }

        e->seq_init = false;
        ext_register_receive_real_samps_task(e->tid, rx_chan);
		return true;
	}
	
	if (sscanf(msg, "SET ft8_protocol=%d", &proto) == 1) {
        if (proto < FT8_PROTOCOL_FT8 || proto > FT8_PROTOCOL_FST4W_1800) {
            rcprintf(rx_chan, "FT8: invalid protocol %d\n", proto);
            return true;
        }
	    e->proto = proto;
        conn_t *conn = rx_channels[e->rx_chan].conn;
		e->last_freq_kHz = conn->freqHz/1e3;
		decode_ft8_protocol(rx_chan, conn->freqHz, proto);
		return true;
	}

	float df;
	n = sscanf(msg, "SET dialfreq=%f", &df);
	if (n == 1) {
		e->arun_dial_freq_kHz = df;
		//rcprintf(rx_chan, "FT8 autorun: dial_freq_kHz=%.6f\n", e->arun_dial_freq_kHz);
		return true;
	}

	if (strcmp(msg, "SET autorun") == 0) {
	    e->autorun = true;
	    return true;
	}

	if (strcmp(msg, "SET ft8_close") == 0) {
		//rcprintf(rx_chan, "FT8 close\n");
		ft8_close(rx_chan);
		return true;
	}
	
	if (strcmp(msg, "SET ft8_test") == 0) {
        if (e->proto != FT8_PROTOCOL_FST4W_120) {
            ext_send_msg_encoded(rx_chan, false, "EXT", "error",
                "Select FST4W-120 before starting the test");
            return true;
        }
        if (ft8_conf.fst4w_test_samples == 0) {
            ext_send_msg_encoded(rx_chan, false, "EXT", "error",
                "FST4W-120 test sample is unavailable");
            return true;
        }
		e->start_test = 0;
        e->test_out_pos = 0;
        e->test_progress = 0;
        e->test_feeding = false;
		e->test = true;
        ext_send_msg(rx_chan, false, "EXT test_status=waiting test_progress=0");
		return true;
	}

	return false;
}

// catch changes to reporter call/grid from admin page FT8 config (also called during initialization)
bool ft8_update_vars_from_config(bool called_at_init_or_restart)
{
    bool update_cfg = false;
    char *s;
    
    cfg_default_object("ft8", "{}", &update_cfg);
    
    // Changing reporter call on admin page requires restart. This is because of
    // conditional behavior at startup, e.g. uploads enabled because valid call is now present
    // or autorun tasks starting for the same reason.
    // ft8_conf.rcall is still updated here to handle the initial assignment and
    // manual changes from FT8 admin page.
    //
    // Also, first-time init of FT8 reporter call/grid from WSPR values
    
    s = (char *) cfg_string("WSPR.callsign", NULL, CFG_REQUIRED);
    cfg_default_string("ft8.callsign", s, &update_cfg);
	cfg_string_free(s);
    s = (char *) cfg_string("ft8.callsign", NULL, CFG_REQUIRED);
    kiwi_ifree(ft8_conf.rcall, "ft8 rcall");
	ft8_conf.rcall = kiwi_str_encode(s);
	cfg_string_free(s);

    s = (char *) cfg_string("WSPR.grid", NULL, CFG_REQUIRED);
    cfg_default_string("ft8.grid", s, &update_cfg);
	cfg_string_free(s);
    s = (char *) cfg_string("ft8.grid", NULL, CFG_REQUIRED);
	kiwi_strncpy(ft8_conf.rgrid, s, LEN_GRID);
	cfg_string_free(s);
    set_reporter_grid((char *) ft8_conf.rgrid);
	grid_to_latLon(ft8_conf.rgrid, &ft8_conf.r_loc);
	if (ft8_conf.r_loc.lat != 999.0)
		latLon_deg_to_rad(ft8_conf.r_loc);
    
    // Make sure ft8.autorun holds *correct* count of non-preemptible autorun processes.
    // If Kiwi was previously configured for a larger rx_chans, and more than rx_chans worth
    // of autoruns were enabled, then with a reduced rx_chans it is essential not to count
    // the ones beyond the rx_chans limit. That's why "i < rx_chans" appears below and
    // not MAX_RX_CHANS.
    if (called_at_init_or_restart) {
        int num_autorun = 0, num_non_preempt = 0;
        for (int instance = 0; instance < rx_chans; instance++) {
            int autorun = cfg_default_int(stprintf("ft8.autorun%d", instance), 0, &update_cfg);
            int preempt = cfg_default_int(stprintf("ft8.preempt%d", instance), 0, &update_cfg);
            //printf("ft8.autorun%d=%d(band=%d) ft8.preempt%d=%d\n", instance, autorun, autorun-1, instance, preempt);
            if (autorun) num_autorun++;
            if (autorun && (preempt == 0)) num_non_preempt++;
            ft8_arun_band[instance] = autorun;
            ft8_arun_preempt[instance] = preempt;
        }
        if (ft8_conf.rcall == NULL || *ft8_conf.rcall == '\0' || ft8_conf.rgrid[0] == '\0') {
            printf("FT8 autorun: reporter callsign and grid square fields must be entered on FT8 section of admin page\n");
            num_autorun = num_non_preempt = 0;
        }
        ft8_conf.num_autorun = num_autorun;
        cfg_update_int("ft8.autorun", num_non_preempt, &update_cfg);
        //printf("FT8 autorun: num_autorun=%d ft8.autorun=%d(non-preempt) rx_chans=%d\n", num_autorun, num_non_preempt, rx_chans);
    }

    ft8_conf.SNR_adj = cfg_default_int("ft8.SNR_adj", -22, &update_cfg);
    if (ft8_conf.SNR_adj == 0) {    // update to new default
        cfg_set_int("ft8.SNR_adj", -22);
        update_cfg = true;
    }
    ft8_conf.dT_adj = cfg_default_int("ft8.dT_adj", -1, &update_cfg);

    ft8_conf.GPS_update_grid = cfg_default_bool("ft8.GPS_update_grid", false, &update_cfg);
    ft8_conf.syslog = cfg_default_bool("ft8.syslog", false, &update_cfg);
    ft8_conf.spot_log = cfg_default_bool("ft8.spot_log", false, &update_cfg);

	//printf("ft8_update_vars_from_config: rcall <%s> ft8_conf.rgrid=<%s> ft8_conf.GPS_update_grid=%d\n", ft8_conf.rcall, ft8_conf.rgrid, ft8_conf.GPS_update_grid);
    return update_cfg;
}

// order matches ft8.autorun_u in FT8.js
// only add new entries to the end so as not to disturb existing values stored in config
static double ft8_cfs[] = {     // usb carrier/dial freq
    /* FT8 */ 1840, 3573, 5357, 7074,   10136, 14074, 18100, 21074, 24915, 28074, 50313, 40680, 60074,
    /* FT4 */       3575.5,     7047.5, 10140, 14080, 18104, 21140, 24919, 28180, 50318,
    /* FST4W */ 137.5, 475.7, 137.5, 475.7, 137.5, 475.7, 137.5, 475.7, 137.5, 475.7, 137.5, 475.7, 137.5, 475.7,
};

static const char* ft8_name[] = {
    "160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m", "8m*", "5m*",
            "80m",        "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m",
    "LF", "MF", "LF", "MF", "LF", "MF", "LF", "MF", "LF", "MF", "LF", "MF", "LF", "MF"
};

static ft8_protocol_e ft8_arun_proto[] = {
    FT8_PROTOCOL_FT8, FT8_PROTOCOL_FT8, FT8_PROTOCOL_FT8, FT8_PROTOCOL_FT8, FT8_PROTOCOL_FT8,
    FT8_PROTOCOL_FT8, FT8_PROTOCOL_FT8, FT8_PROTOCOL_FT8, FT8_PROTOCOL_FT8, FT8_PROTOCOL_FT8,
    FT8_PROTOCOL_FT8, FT8_PROTOCOL_FT8, FT8_PROTOCOL_FT8,
    FT8_PROTOCOL_FT4, FT8_PROTOCOL_FT4, FT8_PROTOCOL_FT4, FT8_PROTOCOL_FT4, FT8_PROTOCOL_FT4,
    FT8_PROTOCOL_FT4, FT8_PROTOCOL_FT4, FT8_PROTOCOL_FT4, FT8_PROTOCOL_FT4,
    FT8_PROTOCOL_FST4W_15, FT8_PROTOCOL_FST4W_15,
    FT8_PROTOCOL_FST4W_30, FT8_PROTOCOL_FST4W_30,
    FT8_PROTOCOL_FST4W_60, FT8_PROTOCOL_FST4W_60,
    FT8_PROTOCOL_FST4W_120, FT8_PROTOCOL_FST4W_120,
    FT8_PROTOCOL_FST4W_300, FT8_PROTOCOL_FST4W_300,
    FT8_PROTOCOL_FST4W_900, FT8_PROTOCOL_FST4W_900,
    FT8_PROTOCOL_FST4W_1800, FT8_PROTOCOL_FST4W_1800
};

void ft8_update_spot_count(int rx_chan, u4_t spot_count)
{
    ft8_t *e = &ft8[rx_chan];
    if (e->autorun) {
        input_msg_internal(e->arun_csnd, (char *) "SET geoloc=%d%%20decoded%s",
            spot_count, e->arun_csnd->arun_preempt? ",%20preemptible" : "");
    }
}

static void ft8_autorun(int instance, bool initial)
{
    rx_util_t *r = &rx_util;
    int band = ft8_arun_band[instance]-1;
    double dial_freq_kHz = ft8_cfs[band];
    ft8_protocol_e proto = ft8_arun_proto[band];
    bool fst4w = (proto >= FT8_PROTOCOL_FST4W_15);
    bool preempt = (ft8_arun_preempt[instance] != ARUN_PREEMPT_NO);
    char *ident_user;
    if (fst4w) {
        asprintf(&ident_user, "FST4W-%d-%s", kFST4_TR_periods[proto - FT8_PROTOCOL_FST4W_15], ft8_name[band]);
    } else {
        asprintf(&ident_user, "FT%d-%s", proto == FT8_PROTOCOL_FT4? 4:8, ft8_name[band]);
    }
    char *geoloc;
    asprintf(&geoloc, "0%%20decoded%s", preempt? ",%20preemptible" : "");

	bool ok = internal_conn_setup(ICONN_WS_SND | ICONN_WS_EXT, &iconn[instance], instance, PORT_BASE_INTERNAL_FT8,
	    WS_FL_IS_AUTORUN | (initial? WS_FL_INITIAL : 0),
        "usb", fst4w? FST4W_PASSBAND_LO : FT8_PASSBAND_LO, fst4w? FST4W_PASSBAND_HI : FT8_PASSBAND_HI,
        dial_freq_kHz, ident_user, geoloc, "FT8");
	if (!ok) {
	    free(ident_user); free(geoloc);
        //printf("FT8 autorun: internal_conn_setup() FAILED instance=%d band=%d %s %.2f\n",
	    //    instance, band, ident_user, dial_freq_kHz);
        return;
    }

    conn_t *csnd = iconn[instance].csnd;
    csnd->arun_preempt = preempt;
    int rx_chan = csnd->rx_channel;
    r->arun_which[rx_chan] = ARUN_FT8;
    r->arun_band[rx_chan] = band;
    ft8_t *e = &ft8[rx_chan];
    ft8_reset(e);
    e->instance = instance;
    e->rx_chan = rx_chan;
    e->arun_csnd = csnd;
    e->arun_dial_freq_kHz = dial_freq_kHz;

	clprintf(csnd, "FT8 autorun: START instance=%d rx_chan=%d band=%d %s %.2f preempt=%d\n",
	    instance, rx_chan, band, ident_user, dial_freq_kHz, preempt);
    free(ident_user); free(geoloc);
	
    conn_t *cext = iconn[instance].cext;
    e->arun_cext = cext;
    input_msg_internal(cext, (char *) "SET autorun");
    input_msg_internal(cext, (char *) "SET dialfreq=%.2f", dial_freq_kHz);
    input_msg_internal(cext, (char *) "SET ft8_start=%d", proto);    // ext task created here
}

void ft8_autorun_start(bool initial)
{
    rx_util_t *r = &rx_util;
    if (ft8_conf.num_autorun == 0) {
        //printf("FT8 autorun_start: none configured\n");
        return;
    }

    for (int instance = 0; instance < rx_chans; instance++) {
        int band = ft8_arun_band[instance];
        if (band == ARUN_REG_USE) continue;     // "regular use" menu entry
        band--;     // make array index
        
        // Is this instance already running on any channel?
        // This loop should never exclude ft8_autorun() when called from ft8_autorun_restart()
        // because all instances were just stopped.
        // When called from rx_autorun_restart_victims() it functions normally.
        int rx_chan;
        for (rx_chan = 0; rx_chan < rx_chans; rx_chan++) {
            if (r->arun_which[rx_chan] == ARUN_FT8 && r->arun_band[rx_chan] == band) {
                //printf("FT8 autorun: instance=%d band=%d %.2f already running on rx%d\n",
                //    instance, band, ft8_cfs[band], rx_chan);
                break;
            }
        }
        if (rx_chan == rx_chans) {
            // arun_{which,band} set only after ft8_autorun():internal_conn_setup() succeeds
            ft8_autorun(instance, initial);
        }
    }
}

void ft8_autorun_restart()
{
    int rx_chan;
    rx_util_t *r = &rx_util;
    ft8_t *ft8_p[MAX_RX_CHANS];

    printf("FT8 autorun: RESTART\n");
    r->arun_suspend_restart_victims = true;
        // shutdown all
        for (rx_chan = 0; rx_chan < rx_chans; rx_chan++) {
            ft8_p[rx_chan] = NULL;
            if (r->arun_which[rx_chan] == ARUN_FT8) {
                ft8_t *e = &ft8[rx_chan];
                ft8_p[rx_chan] = e;
                internal_conn_shutdown(&iconn[e->instance]);
                //printf("FT8 autorun: rx_chan=%d ARUN_FT8 => ARUN_NONE\n", rx_chan);
                r->arun_which[rx_chan] = ARUN_NONE;
            }
        }
        rx_autorun_clear();
        TaskSleepReasonSec("ft8_autorun_stop", 3);      // give time to disconnect
    
        // reset only autorun instances identified above (there may be non-autorun FT8 extensions running)
        for (rx_chan = 0; rx_chan < rx_chans; rx_chan++) {
            if (ft8_p[rx_chan] != NULL) ft8_reset(ft8_p[rx_chan]);
        }
        memset(iconn, 0, sizeof(internal_conn_t));
        
        // bring ft8_arun_band[] and ft8_arun_preempt[] up-to-date
        ft8_update_vars_from_config(true);
        
        // restart all enabled
        ft8_autorun_start(true);
    r->arun_suspend_restart_victims = false;
}

void FT8_main();

void ft8_test_complete(int rx_chan)
{
    ft8_t *e = &ft8[rx_chan];
    if (!e->test) return;

    e->test = false;
    e->start_test = 0;
    e->test_feeding = false;
    e->test_progress = 100;
    ext_send_msg(rx_chan, false, "EXT test_status=complete test_progress=100");
}

static void ft8_load_fst4w_test()
{
    const char *fn = DIR_SAMPLES "/FST4W-120.raw";
    int fd = open(fn, O_RDONLY);
    if (fd < 0) {
        printf("FT8: optional FST4W-120 test sample not found: %s\n", fn);
        return;
    }

    off_t size = kiwi_file_size(fn);
    if (size != FST4W_TEST_PERIOD * FST4W_TEST_SAMPLE_RATE * (int) sizeof(s2_t)) {
        printf("FT8: invalid FST4W-120 test sample size: %lld\n", (long long) size);
        close(fd);
        return;
    }

    void *file = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file == MAP_FAILED) {
        printf("FT8: mmap failed for %s\n", fn);
        return;
    }

    ft8_conf.fst4w_test_start = (s2_t *) file;
    ft8_conf.fst4w_test_samples = size / sizeof(s2_t);
    printf("FT8: loaded %d FST4W-120 test samples from %s\n",
        ft8_conf.fst4w_test_samples, fn);
}

ext_t ft8_ext = {
	"FT8",
	FT8_main,
	ft8_close,
	ft8_msgs,
	EXT_NEW_VERSION,
	EXT_FLAGS_HEAVY
};

void FT8_main()
{
	ext_register(&ft8_ext);
    ft8_update_vars_from_config(false);
    PSKReporter_init();
    ft8_autorun_start(true);
    ft8_load_fst4w_test();
}
