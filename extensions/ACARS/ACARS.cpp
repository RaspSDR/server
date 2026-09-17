// Copyright (c) 2026 RaspSDR contributors

#include "ACARS.h"
#include "data_pump.h"
#include "ext.h"
#include "mem.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static acars_t acars;
static acars_chan_t acars_chan[MAX_RX_CHANS];

static void acars_process_clear(acars_chan_t *e)
{
    if (e->input_pipe >= 0) {
        close(e->input_pipe);
        e->input_pipe = -1;
    }
    if (e->output_pipe >= 0) {
        close(e->output_pipe);
        e->output_pipe = -1;
    }
    e->pid = 0;
}

static void acars_process_stop(acars_chan_t *e)
{
    pid_t pid = e->pid;

    if (pid > 0)
        kill(pid, SIGTERM);

    acars_process_clear(e);

    if (pid > 0) {
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
            ;
    }
}

static bool acars_process_start(acars_chan_t *e)
{
    int in_pipe[2] = { -1, -1 };
    int out_pipe[2] = { -1, -1 };

    if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0) {
        rcprintf(e->rx_chan, "ACARS: pipe failed: %s\n", strerror(errno));
        if (in_pipe[0] >= 0) close(in_pipe[0]);
        if (in_pipe[1] >= 0) close(in_pipe[1]);
        if (out_pipe[0] >= 0) close(out_pipe[0]);
        if (out_pipe[1] >= 0) close(out_pipe[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        rcprintf(e->rx_chan, "ACARS: fork failed: %s\n", strerror(errno));
        if (in_pipe[0] >= 0) close(in_pipe[0]);
        if (in_pipe[1] >= 0) close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return false;
    }

    if (pid == 0) {
        close(in_pipe[1]);
        dup2(in_pipe[0], STDIN_FILENO);
        close(in_pipe[0]);

        close(out_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(out_pipe[1]);

        execl(ACARSDEC_BIN, ACARSDEC_BIN, NULL);
        _exit(127);
    }

    close(in_pipe[0]);
    e->input_pipe = in_pipe[1];
    close(out_pipe[1]);
    e->output_pipe = out_pipe[0];
    e->pid = pid;
    return true;
}

static void acars_test_file_data(int rx_chan, int instance, int nsamps, TYPEMONO16 *samps, int freqHz)
{
    (void) instance;
    (void) freqHz;
    acars_chan_t *e = &acars_chan[rx_chan];
    if (!e->test)
        return;

    for (int i = 0; i < nsamps; i++) {
        if (e->test_ptr < acars.sample_end)
            samps[i] = *e->test_ptr++;
        else
            samps[i] = 0;
    }

    if (e->test_ptr >= acars.sample_end) {
        e->test = false;
        ext_send_msg(rx_chan, false, "EXT test_done");
    }
}

static void acars_feed_task(void *param)
{
    while (1) {
        int rx_chan = (int) FROM_VOID_PARAM(TaskSleepReason("wait for ACARS samples"));
        acars_chan_t *e = &acars_chan[rx_chan];
        rx_dpump_t *rx = &rx_dpump[rx_chan];

        while ((u4_t) e->rd_pos != rx->real_wr_pos) {
            if (rx->real_seqnum[e->rd_pos] != e->seq) {
                if (e->seq_init) {
                    u4_t got = rx->real_seqnum[e->rd_pos];
                    rcprintf(rx_chan, "ACARS SEQ: @%d got %u expecting %u (%d)\n",
                        e->rd_pos, got, e->seq, (int) (got - e->seq));
                } else {
                    e->seq_init = true;
                }
                e->seq = rx->real_seqnum[e->rd_pos];
            }
            e->seq++;

            int rd_pos = e->rd_pos;
            if (e->input_pipe >= 0) {
                const char *data = (const char *) &rx->real_samples_s2[rd_pos][0];
                size_t remaining = sizeof(s2_t) * FASTFIR_OUTBUF_SIZE;

                while (remaining > 0) {
                    ssize_t written = write(e->input_pipe, data, remaining);
                    if (written < 0 && errno == EINTR)
                        continue;
                    if (written <= 0) {
                        rcprintf(rx_chan, "ACARS: decoder input closed\n");
                        close(e->input_pipe);
                        e->input_pipe = -1;
                        break;
                    }
                    data += written;
                    remaining -= written;
                }
            }
            e->rd_pos = (rd_pos + 1) & (N_DPBUF - 1);
        }
    }
}

static void acars_decoder_task(void *param)
{
    int rx_chan = (int) FROM_VOID_PARAM(param);
    acars_chan_t *e = &acars_chan[rx_chan];

    while (e->running) {
        if (!acars_process_start(e)) {
            ext_send_msg_encoded(rx_chan, false, "EXT", "error",
                "Unable to start %s", ACARSDEC_BIN);
            TaskSleepMsec(1000);
            continue;
        }

        ext_send_msg(rx_chan, false, "EXT decoder=live");

        while (e->running && e->output_pipe >= 0) {
            char buffer[2048];
            ssize_t n = read(e->output_pipe, buffer, sizeof(buffer));
            if (n > 0) {
                ext_send_msg_encoded(rx_chan, false, "EXT", "chars", "%.*s", (int) n, buffer);
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            break;
        }

        pid_t pid = e->pid;
        acars_process_clear(e);
        if (pid > 0) {
            while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
                ;
        }

        if (e->running)
            ext_send_msg_encoded(rx_chan, false, "EXT", "error",
                "ACARS decoder stopped; restarting");
    }
}

static void acars_close(int rx_chan)
{
    acars_chan_t *e = &acars_chan[rx_chan];
    e->running = false;

    ext_unregister_receive_real_samps(rx_chan);
    ext_unregister_receive_real_samps_task(rx_chan);
    ext_unregister_receive_cmds(rx_chan);
    acars_process_stop(e);

    if (e->feed_tid) {
        TaskRemove(e->feed_tid);
        e->feed_tid = 0;
    }
    if (e->decoder_tid) {
        TaskRemove(e->decoder_tid);
        e->decoder_tid = 0;
    }
}

static bool acars_receive_cmds(u2_t key, char *cmd, int rx_chan)
{
    if (key != CMD_TUNE)
        return false;

    char *mode_m;
    double locut, hicut, freq;
    int mparam;
    int n = sscanf(cmd, "SET mod=%16ms low_cut=%lf high_cut=%lf freq=%lf param=%d",
        &mode_m, &locut, &hicut, &freq, &mparam);
    if (n != 4 && n != 5)
        return false;

    acars_chan[rx_chan].tuned_f = freq;
    ext_send_msg(rx_chan, false, "EXT freq=%.3f", freq / 1000.0);
    kiwi_asfree(mode_m);
    return true;
}

static bool acars_msgs(char *msg, int rx_chan)
{
    acars_chan_t *e = &acars_chan[rx_chan];

    if (strcmp(msg, "SET ext_server_init") == 0) {
        memset(e, 0, sizeof(*e));
        e->rx_chan = rx_chan;
        e->input_pipe = -1;
        e->output_pipe = -1;
        ext_send_msg(rx_chan, false, "EXT ready");
        return true;
    }

    if (strcmp(msg, "SET start") == 0) {
        if (access(ACARSDEC_BIN, X_OK) != 0) {
            ext_send_msg_encoded(rx_chan, false, "EXT", "error",
                "Decoder not installed at %s", ACARSDEC_BIN);
            return true;
        }

        if (e->running)
            return true;

        e->running = true;
        e->seq_init = false;
        e->feed_tid = CreateTaskF(acars_feed_task, TO_VOID_PARAM(rx_chan),
            EXT_PRIORITY, CTF_RX_CHANNEL | (rx_chan & CTF_CHANNEL));
        e->decoder_tid = CreateTaskF(acars_decoder_task, TO_VOID_PARAM(rx_chan),
            EXT_PRIORITY, CTF_RX_CHANNEL | (rx_chan & CTF_CHANNEL));
        ext_register_receive_real_samps_task(e->feed_tid, rx_chan);
        ext_register_receive_cmds(acars_receive_cmds, rx_chan);
        return true;
    }

    if (strcmp(msg, "SET stop") == 0) {
        acars_close(rx_chan);
        return true;
    }

    if (strcmp(msg, "SET test") == 0) {
        if (!acars.sample_start) {
            ext_send_msg_encoded(rx_chan, false, "EXT", "error",
                "Test sample is missing");
            return true;
        }
        e->test_ptr = acars.sample_start;
        e->test = true;
        ext_register_receive_real_samps(acars_test_file_data, rx_chan);
        ext_send_msg(rx_chan, false, "EXT decoder=test");
        return true;
    }

    return false;
}

void ACARS_main();

static ext_t acars_ext = {
    "ACARS",
    ACARS_main,
    acars_close,
    acars_msgs,
    EXT_NEW_VERSION,
    EXT_FLAGS_HEAVY
};

void ACARS_main()
{
#ifdef ACARS
    ext_register(&acars_ext);

    const char *fn = DIR_SAMPLES "/Acars_sample.raw";
    int fd = open(fn, O_RDONLY);
    if (fd < 0) {
        printf("ACARS: optional test sample not found: %s\n", fn);
        return;
    }

    off_t size = kiwi_file_size(fn);
    if (size <= 0 || (size & 1)) {
        printf("ACARS: invalid test sample size: %lld\n", (long long) size);
        close(fd);
        return;
    }

    void *file = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file == MAP_FAILED) {
        printf("ACARS: mmap failed for %s\n", fn);
        return;
    }

    acars.sample_start = (s2_t *) file;
    acars.sample_count = size / sizeof(s2_t);
    acars.sample_end = acars.sample_start + acars.sample_count;
    printf("ACARS: loaded %d test samples from %s\n", acars.sample_count, fn);
#else
    printf("ext_register: \"ACARS\" not configured\n");
#endif
}
