#include "ext.h" // all calls to the extension interface begin with "ext_", e.g. ext_register()

#include "kiwi.h"
#include "coroutines.h"
#include "data_pump.h"
#include "mem.h"

#include "skimmer.hpp"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>

//#define DEBUG_MSG true
#define DEBUG_MSG false

void CW_skimmer_main();
static void CW_skimmer_close(int rx_chan);
static bool CW_skimmer_msgs(char* msg, int rx_chan);

struct skimmer_state {
    CwSkimmer skimmer;
    tid_t tid;
    int chan;

    u4_t rd_pos;
    bool seq_init;
    u4_t seq;

    bool test;
    int nsamps;
    s2_t* s2p;
} states[MAX_RX_CHANS];

typedef struct {
    s2_t *s2p_start, *s2p_end;
    int tsamps;
} cw_conf_t;

static cw_conf_t cw_conf;

ext_t cw_skimmer_ext = {
    "CW_skimmer",
    CW_skimmer_main,
    CW_skimmer_close,
    CW_skimmer_msgs,
    EXT_NEW_VERSION,
    EXT_FLAGS_HEAVY
};

void CW_skimmer_main() {
    ext_register(&cw_skimmer_ext);
    // const char *fn = cfg_string("CW.test_file", NULL, CFG_OPTIONAL);
    const char* fn = "CW.test.au";
    if (!fn || *fn == '\0') return;
    char* fn2;
    asprintf(&fn2, "%s/%s", DIR_SAMPLES, fn);
    // cfg_string_free(fn);
    printf("CW_Skimmer: mmap %s\n", fn2);
    int fd = open(fn2, O_RDONLY);
    if (fd < 0) {
        printf("CW_Skimmer: open failed\n");
        return;
    }
    off_t fsize = kiwi_file_size(fn2);
    kiwi_asfree(fn2);
    char* file = (char*)mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file == MAP_FAILED) {
        printf("CW_Skimmer: mmap failed\n");
        return;
    }
    close(fd);
    int words = fsize / 2;
    cw_conf.s2p_start = (s2_t*)file;
    u4_t off = *(cw_conf.s2p_start + 3);
    off = FLIP16(off);
    printf("CW_Skimmer: off=%d size=%lld\n", off, fsize);
    off /= 2;
    cw_conf.s2p_start += off;
    words -= off;
    cw_conf.s2p_end = cw_conf.s2p_start + words;
    cw_conf.tsamps = words;
}

static void CW_skimmer_close(int rx_chan) {
    skimmer_state* e = &states[rx_chan];

    ext_unregister_receive_real_samps_task(rx_chan);

    if (e->tid != 0) {
        printf("CW_Skimmer: TaskRemove\n");
        TaskRemove(e->tid);
        e->tid = 0;
    }
}

static void cw_file_data(int rx_chan, int chan, int nsamps, TYPEMONO16* samps, int freqHz) {
    skimmer_state* e = &states[rx_chan];

    if (!e->test) return;

    if (e->test) {
        // real_printf("#"); fflush(stdout);
        for (int i = 0; i < nsamps; i++) {
            if (e->s2p > cw_conf.s2p_end) {
                e->s2p = cw_conf.s2p_start;
                e->nsamps = 0;
            }
            *samps++ = (s2_t)FLIP16(*e->s2p);
            e->s2p++;
        }

        int pct = e->nsamps * 100 / cw_conf.tsamps;
        e->nsamps += nsamps;
        pct = CLAMP(pct, 3, 100);
        ext_send_msg(rx_chan, false, "EXT bar_pct=%d", pct);
    }
}

static void cw_skimmer_task(void* param) {
    while (1) {

        int rx_chan = (int)FROM_VOID_PARAM(TaskSleepReason("wait for wakeup"));

        skimmer_state* e = &states[rx_chan];
        rx_dpump_t* rx = &rx_dpump[rx_chan];

        while (e->rd_pos != rx->real_wr_pos) {
            if (rx->real_seqnum[e->rd_pos] != e->seq) {
                if (!e->seq_init) {
                    e->seq_init = true;
                }
                else {
                    u4_t got = rx->real_seqnum[e->rd_pos], expecting = e->seq;
                    printf("CWSkimmer rx%d SEQ: @%d got %d expecting %d (%d)\n", rx_chan, e->rd_pos, got, expecting, got - expecting);
                }
                e->seq = rx->real_seqnum[e->rd_pos];
            }
            e->seq++;

            // real_printf("%d ", e->rd_pos); fflush(stdout);
            int rd_pos = e->rd_pos;
            e->skimmer.AddSamples(&rx->real_samples_s2[rd_pos][0], FASTFIR_OUTBUF_SIZE);
            e->rd_pos = (rd_pos + 1) & (N_DPBUF - 1);
        }
    }
}

bool CW_skimmer_msgs(char* msg, int rx_chan) {
    skimmer_state* e = &states[rx_chan];
    // printf("### cw_decoder_msgs RX%d <%s>\n", rx_chan, msg);

    if (strcmp(msg, "SET ext_server_init") == 0) {
        e->chan = rx_chan;	// remember our receiver channel number
        e->skimmer.SetCallback([e](int freq, char ch) {
            ext_send_msg_encoded(e->chan, DEBUG_MSG, "EXT", "cw_chars", "%c%d", ch, freq);
        });

        ext_send_msg(rx_chan, DEBUG_MSG, "EXT ready");
        return true;
    }

    if (strcmp(msg, "SET cw_start") == 0) {

        if (cw_conf.tsamps != 0) {
            ext_register_receive_real_samps(cw_file_data, rx_chan);
        }

        if (e->tid == 0) {
            e->tid = CreateTaskF(cw_skimmer_task, TO_VOID_PARAM(rx_chan), EXT_PRIORITY, CTF_RX_CHANNEL | (rx_chan & CTF_CHANNEL));
        }

        e->seq_init = false;
        ext_register_receive_real_samps_task(e->tid, rx_chan);
        // ext_register_receive_real_samps(CwDecode_RxProcessor, rx_chan);

        return true;
    }

    if (strcmp(msg, "SET cw_stop") == 0) {
        e->skimmer.flush();
        e->test = false;
        return true;
    }

    int test;
    if (sscanf(msg, "SET cw_test=%d", &test) == 1) {
        printf("CW rx%d test=%d\n", rx_chan, test);
        e->s2p = cw_conf.s2p_start;
        e->nsamps = 0;
        e->test = test ? true : false;
        return true;
    }

    return false;
}