// Copyright (c) 2021 John Seamons, ZL4VO/KF6VO

#include "HFDL.h"
#include <sys/types.h>
#include <sys/wait.h>

#define SAMPLES_CF32

static hfdl_t hfdl;
static hfdl_chan_t hfdl_chan[MAX_RX_CHANS];

static void hfdl_pushback_file_data(int rx_chan, int instance, int nsamps, TYPECPX *samps)
{
    hfdl_chan_t *e = &hfdl_chan[rx_chan];

    if (!e->test) return;
    if (e->s2p >= hfdl.s2p_end) {
        //printf("HFDL: pushback test file DONE ch=%d tid=%d\n", rx_chan, e->input_tid);

        ext_send_msg(rx_chan, false, "EXT test_done");
        e->test = false;
        ext_unregister_receive_iq_samps(rx_chan);
        return;
    }
    
    // Pushback 12 kHz sample file so it sounds right.
    // Have hfdl_task() do 12k => 36k resampling before calling decoder (which requires 36 kHz sampling)
    for (int i = 0; i < nsamps; i++) {
        if (e->s2p < hfdl.s2p_end) {
            samps->re = (TYPEREAL) (s4_t) *e->s2p;
            e->s2p++;
            samps->im = (TYPEREAL) (s4_t) *e->s2p;
            e->s2p++;
            samps++;
        }
    }

    int pct = e->nsamps * 100 / hfdl.tsamps;
    e->nsamps += nsamps;
    pct += 3;
    if (pct > 100) pct = 100;
    ext_send_msg(rx_chan, false, "EXT bar_pct=%d", pct);
}

static void hfdl_task(void *param)
{
    while (1) {
        int rx_chan = (int)FROM_VOID_PARAM(TaskSleepReason("wait for wakeup"));

        hfdl_chan_t *e = &hfdl_chan[rx_chan];
        iq_buf_t *rx = &RX_SHMEM->iq_buf[rx_chan];
    
        // blocks via TaskSleep() / wakeup due to ext_register_receive_iq_samps_task()
        while (e->rd_pos != rx->iq_wr_pos) {
            if (rx->iq_seqnum[e->rd_pos] != e->seq) {
                if (!e->seq_init) {
                    e->seq_init = true;
                } else {
                    u4_t got = rx->iq_seqnum[e->rd_pos], expecting = e->seq;
                    rcprintf(rx_chan, "HFDL SEQ: @%d got %d expecting %d (%d)\n", e->rd_pos, got, expecting, got - expecting);
                }
                e->seq = rx->iq_seqnum[e->rd_pos];
            }
            e->seq++;
    
            if (e->reset) {
                e->reset = false;
            }

            // TODO: Output to stdin of the dumper process
            // if (samps_c) *samps_c = &rx->iq_samples[e->rd_pos][0];
            int rd_pos = e->rd_pos;
            if (e->input_pipe) {
                // write to the dumper process
                size_t total_written = 0;
                size_t to_write = sizeof(TYPECPX) * FASTFIR_OUTBUF_SIZE;
                char *data_ptr = (char *)&rx->iq_samples[e->rd_pos][0];

                while (total_written < to_write) {
                    ssize_t n = write(e->input_pipe, data_ptr + total_written, to_write - total_written);
                    if (n < 0) {
                        perror("HFDL: write() failed");
                        break;
                    }
                    total_written += n;
                }

                if (total_written != to_write) {
                    printf("HFDL: write() incomplete\n");
                    break;
                }
            }

            e->rd_pos = (rd_pos+1) & (N_DPBUF-1);
        }
    }
}

void hfdl_close(int rx_chan)
{
	hfdl_chan_t *e = &hfdl_chan[rx_chan];
    printf("HFDL: close tid=%d dumphfdl_tid=%d\n", e->tid, e->dumphfdl_tid);

    ext_unregister_receive_iq_samps_task(e->rx_chan);
    
	if (e->tid) {
        //printf("HFDL: TaskRemove dumphfdl_task\n");
		TaskRemove(e->tid);
		e->tid = 0;
	}

    if (e->input_pipe)
    {
        // kill(e->pid, SIGTERM);
        close(e->input_pipe);
        close(e->output_pipe);

        // check if the process exit
        waitpid(e->pid, NULL, 0);

        e->input_pipe = 0;
        e->output_pipe = 0;
        e->pid = 0;
    }

    ext_unregister_receive_cmds(e->rx_chan);
}

bool hfdl_receive_cmds(u2_t key, char *cmd, int rx_chan)
{
    int i, n;
    
    if (key == CMD_TUNE) {
        char *mode_m;
        double locut, hicut, freq;
        int mparam;
        n = sscanf(cmd, "SET mod=%16ms low_cut=%lf high_cut=%lf freq=%lf param=%d", &mode_m, &locut, &hicut, &freq, &mparam);
        if (n == 4 || n == 5) {
	        hfdl_chan_t *e = &hfdl_chan[rx_chan];
	        e->tuned_f = freq;
            printf("HFDL: CMD_TUNE freq=%.2f mode=%s\n", freq, mode_m);
            kiwi_asfree(mode_m);
            return true;
        }
    }
    
    return false;
}

static void dumphfdl_task(void *param)
{
    int in_pipe[2], out_pipe[2];
    int rx_chan = (int) FROM_VOID_PARAM(param);
    hfdl_chan_t *e = &hfdl_chan[rx_chan];
    pid_t pid;

    if (pipe(in_pipe) == -1 || pipe(out_pipe) == -1) {
        printf("HFDL: pipe() failed\n");
        return;
    }

    pid = fork();
    if (pid == -1) {
        printf("HFDL: fork() failed\n");
        return;
    }

    if (pid == 0) { // Child process
        close(in_pipe[1]);  // Close unused write end
        close(out_pipe[0]); // Close unused read end

        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);

        close(in_pipe[0]);
        close(out_pipe[1]);

        execlp("/media/mmcblk0p1/dumphfdl",     
            "/media/mmcblk0p1/dumphfdl", "--system-table", DIR_SAMPLES "/HFDL_systable.conf",
            "--iq-file", "-",
        
            #ifdef SAMPLES_CF32
                "--sample-format", "CF32",
            #endif
        
            #ifdef SAMPLES_CS16
                "--sample-format", "CS16",
            #endif
        
            "--sample-rate", "12000", "--centerfreq", "0", "0", NULL
        ); // Replace with the target process
        perror("execlp");
        exit(EXIT_FAILURE);
    } else {
        close(in_pipe[0]);  // Close unused read end
        close(out_pipe[1]); // Close unused write end

        e->input_pipe = in_pipe[1];
        e->output_pipe = out_pipe[0];
        e->pid = pid;

        // mark the process is ready to accept data
        while(1) {
            char buffer[1024];
            ssize_t n = read(e->output_pipe, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                ext_send_msg_encoded(e->rx_chan, false, "EXT", "chars", "%.*s", n, buffer);
            }
        }
    }

    e->dumphfdl_tid = 0;
}
    
bool hfdl_msgs(char *msg, int rx_chan)
{
	hfdl_chan_t *e = &hfdl_chan[rx_chan];
	int n;
	
	//printf("### hfdl_msgs RX%d <%s>\n", rx_chan, msg);
	
	if (strcmp(msg, "SET ext_server_init") == 0) {
		e->rx_chan = rx_chan;	// remember our receiver channel number

		ext_send_msg(e->rx_chan, false, "EXT ready");
		return true;
	}

	if (strcmp(msg, "SET start") == 0) {
		//printf("HFDL: start\n");
        //c2s_waterfall_no_sync(e->rx_chan, true);

        e->s2p = e->s2px = e->s22p = hfdl.s2p_start;

        if (!e->tid) {
            e->seq_init = false;
			e->tid = CreateTaskF(hfdl_task, TO_VOID_PARAM(rx_chan), EXT_PRIORITY, CTF_RX_CHANNEL | (rx_chan & CTF_CHANNEL));
        }
		
		ext_register_receive_iq_samps_task(e->tid, rx_chan, POST_AGC);

        if (!e->dumphfdl_tid)
            e->dumphfdl_tid = CreateTaskF(dumphfdl_task, TO_VOID_PARAM(rx_chan), EXT_PRIORITY, CTF_RX_CHANNEL | (rx_chan & CTF_CHANNEL));

		ext_register_receive_cmds(hfdl_receive_cmds, rx_chan);
		return true;
	}

	if (strcmp(msg, "SET stop") == 0) {
		//printf("HFDL: stop\n");
        //c2s_waterfall_no_sync(e->rx_chan, false);
		hfdl_close(e->rx_chan);
		e->test = false;
		return true;
	}
	
	if (strcmp(msg, "SET reset") == 0) {
		//printf("HFDL: RESET\n");
		e->reset = true;
		e->test = false;
		return true;
	}
	
	if (strcmp(msg, "SET send_lowres_latlon") == 0) {
	    if (kiwi.lowres_lat != 0 || kiwi.lowres_lon != 0)
            ext_send_msg_encoded(rx_chan, false, "EXT", "lowres_latlon", "[%d,%d]", kiwi.lowres_lat, kiwi.lowres_lon);
		return true;
	}
	
	double freq;
	if (sscanf(msg, "SET display_freq=%lf", &freq) == 1) {
		rcprintf(rx_chan, "HFDL: display_freq=%.2lf\n", freq);
        #ifdef HFDL
		//    dumphfdl_set_freq(rx_chan, freq);
		#endif
		return true;
	}

	if (sscanf(msg, "SET tune=%lf", &freq) == 1) {
		conn_t *conn = rx_channels[rx_chan].conn;
        input_msg_internal(conn, "SET mod=x low_cut=0 high_cut=0 freq=%.2f", freq);
		e->freq = freq;
		return true;
	}
	
	double test_f;
	if (sscanf(msg, "SET test=%lf", &test_f) == 1) {
        e->s2p = e->s2px = e->s22p = hfdl.s2p_start;
        e->test_f = test_f;
        e->nsamps = 0;

        //bool test = (test_f != 0) && (snd_rate != SND_RATE_3CH);
        bool test = (test_f != 0);
        //printf("HFDL: test=%d test_f=%.2f\n", test, e->test_f);
        if (test) {
            // misuse ext_register_receive_iq_samps() to pushback audio samples from the test file
            ext_register_receive_iq_samps(hfdl_pushback_file_data, rx_chan, PRE_AGC);
            e->test = true;
        } else {
            e->test = false;
            ext_unregister_receive_iq_samps(rx_chan);
        }
		return true;
	}
	
	return false;
}

void HFDL_main();

ext_t hfdl_ext = {
	"HFDL",
	HFDL_main,
	hfdl_close,
	hfdl_msgs,
};

void HFDL_main()
{
    hfdl.nom_rate = snd_rate;

    #ifdef HFDL
	    ext_register(&hfdl_ext);
	#else
        printf("ext_register: \"HFDL\" not configured\n");
	    return;
	#endif

    int n;
    char *file;
    int fd;
    const char *fn;
    
    fn = DIR_SAMPLES "/HFDL_test_12k_cf13270_df13270.wav";
    printf("HFDL: mmap %s\n", fn);
    scall("hfdl open", (fd = open(fn, O_RDONLY)));
    off_t fsize = kiwi_file_size(fn);
    printf("HFDL: size=%lld\n", fsize);
    file = (char *) mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file == MAP_FAILED) sys_panic("HFDL mmap");
    close(fd);
    int words = fsize/2;
    hfdl.s2p_start = (s2_t *) file;
    hfdl.s2p_end = hfdl.s2p_start + words;
    hfdl.tsamps = words / NIQ;
}