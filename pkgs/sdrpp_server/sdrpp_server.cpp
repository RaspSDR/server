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

// Copyright (c) 2024 John Seamons, ZL4VO/KF6VO

#include "sdrpp_server.h"
#include "spyserver_protocol.h"
#include "kiwi.h"
#include "data_pump.h"
#include "printf.h"
#include "rx/rx.h"
#include "rx/rx_util.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

//hf+ sdr://143.178.161.182:5555
//rtl sdr://94.208.173.143:5555

#define SDRPP_PRINTF
#ifdef SDRPP_PRINTF
	#define sdrpp_prf(fmt, ...) \
		lprintf(fmt, ## __VA_ARGS__)
#else
	#define sdrpp_prf(fmt, ...)
#endif

#define SDRPP_DPRINTF
#ifdef SDRPP_DPRINTF
	#define sdrpp_dprf(fmt, ...) \
		printf(fmt, ## __VA_ARGS__)
#else
	#define sdrpp_dprf(fmt, ...)
#endif

typedef struct {
    u2_t i, q;
} iq16_t;

typedef struct {
    u1_t i, q;
} iq8_t;

/* iq8 bytes: 75k 1472, 150k 2944, 300k 5888 (2944 * iq8_t)
    75/1472 = 96/x, 1472*96/75 = 1884.16
*/

#define FNAME DIR_CFG "/samples/CQWW_CW_2005.iq16b.fs96k.cf7040.full.iq"

// options
//#define FILE_IN
#ifdef FILE_IN
    #define PACER
    #define SWAP_IQ         // CQWW file is IQ swapped
#else
    //#define SOURCE_MMAP
    #ifdef SOURCE_MMAP
        #define PACER
        #define SWAP_IQ     // CQWW file is IQ swapped
    #else
        #define DIRECT
        //#define CAMP
    #endif
#endif

#define IQ16
#define FLIP_IQ

#define N_SAMPS (960 * 10)

#ifdef IQ16
    #define SIZEOF_SAMP sizeof(iq16_t)
#else
    #define SIZEOF_SAMP sizeof(iq8_t)
#endif

static u4_t bw;
static int fd = -1, streaming_enabled;
static tid_t sdrpp_WR_tid = -1;
static iq16_t *in;

#ifdef FILE_IN
    static iq16_t file[N_SAMPS];
#else
    #ifdef SOURCE_MMAP
        static off_t fsize;
        static iq16_t *fp;
    #else
        #ifdef CAMP
            static u4_t our_rd_pos = 0;
            #define RD_POS our_rd_pos
        #else
            #define RD_POS rx->rd_pos
        #endif
    #endif
#endif

static void sdrpp_WR(void *param)
{
    int sock_fd = (int) FROM_VOID_PARAM(param);
    
    while (1) {
        if (streaming_enabled) {
            void sdrpp_out(int sock_fd, void *ev_data);
            sdrpp_out(sock_fd, NULL);
        } else {
            #ifdef CAMP
                //real_printf("W"); fflush(stdout);
                TaskSleepMsec(250);
            #else
                //real_printf("W"); fflush(stdout);
                TaskSleepMsec(250);
            #endif
        }
        
        #ifdef PACER
            TaskSleepMsec(1);
        #else
            NextTask("sdrpp_WR");
        #endif
    }
}

// server => client (init)
void sdrpp_accept(int sock_fd)
{
    u4_t srate, freq, f_min, f_max;

    #ifdef PACER
        bw = 96000;
        srate = bw * 2;
        freq = 7040000;
        f_min = freq - bw/2;
        f_max = freq + bw/2;
    
        fd = open(FNAME, O_RDONLY);
        if (fd < 0) panic("sdrpp: open");
        
        #ifdef SOURCE_MMAP
            fsize = kiwi_file_size(FNAME);
            fp = (iq16_t *) mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
            if (fp == MAP_FAILED) {
                panic("sdrpp: mmap failed\n");
            }
            close(fd);
            fd = -1;
            in = fp;
            sdrpp_dprf("sdrpp: %d|%d %p|%p\n", N_SAMPS, fsize / sizeof(iq16_t), in + N_SAMPS, fp + (fsize / sizeof(iq16_t)));
        #endif

        sdrpp_WR_tid = CreateTask(sdrpp_WR, TO_VOID_PARAM(mc), SND_PRIORITY);
    #endif

    #ifdef DIRECT
        rx_chan_t *rxc = &rx_channels[0];
        bw = snd_rate;
        srate = bw * 2;
        freq = 14040000;
        f_min = freq - bw/2;
        f_max = freq + bw/2;
        if (nrx_samps > N_SAMPS) panic("nrx_samps > N_SAMPS");
    
        #ifdef CAMP
        #else
            if (rxc->busy) {
                close(sock_fd);
                return;
            }
        #endif
        
        if (sdrpp_WR_tid != -1) {
            TaskRemove(sdrpp_WR_tid);
            sdrpp_WR_tid = -1;
        }
        
        #ifdef CAMP
        #else
            c2s_sound_init();
            data_pump_start_stop();
        #endif
        
        sdrpp_WR_tid = CreateTask(sdrpp_WR, TO_VOID_PARAM(sock_fd), SND_PRIORITY);

        #ifdef CAMP
        #else
            rxc->busy = true;
            rx_enable(0, RX_CHAN_ENABLE);
            rx_enable(0, RX_DATA_ENABLE);
        #endif
    #endif

// hf+
//0000: a406 0002 0000 0000 0000 0000 0000 0000 3000 0000>0200 0000 3230 3131 00b8 0b00  ................0.......2011....
//0020: 2012 0a00 0800 0000 0000 0000 0000 0000 0000 0000 00f1 5365 1000 0000 0200 0000  .....................Se........
//0040: 0100 0000                                                                        ....

// rtl
//0000: 1e07 0002 0000 0000 0000 0000 0000 0000 3000 0000>0300 0000 0100 0000 009f 2400  ................0.............$.
//0020: 8084 1e00 0900 0000 0000 0000 1d00 0000 0036 6e01 00d2 496b 0800 0000 0000 0000  .................6n...Ik........
//0040: 0000 0000                                                                        ....
    struct SpyServerDeviceInfo di;
    di.mh.ProtocolID = SPYSERVER_PROTOCOL_VERSION;
    di.mh.MessageType = SPYSERVER_MSG_TYPE_DEVICE_INFO;
    di.mh.StreamType = SPYSERVER_STREAM_TYPE_STATUS;
    di.mh.SequenceNumber = 0;
    di.mh.BodySize = DI_SIZE;     // 0x30 12*4=48
                                            // rtl      hf+
    di.DeviceType = SPYSERVER_DEVICE_RTLSDR;  // 3        2
    di.DeviceSerial = 0;                      // 1        0x31313032
    di.MaximumSampleRate = srate;             // 2.4M     768k
    di.MaximumBandwidth = bw;                 // 2M       660k
    di.DecimationStageCount = 1;              // 9        8
    di.GainStageCount = 0;                    // 0        0
    di.MaximumGainIndex = 1;                  // 29       0
    di.MinimumFrequency = f_min;              // 24M      0
    di.MaximumFrequency = f_max;              // 1800M    1700M
    di.Resolution = 1;                        // 8        16
    di.MinimumIQDecimation = 1;               // 0        2
    di.ForcedIQFormat = 1;                     // 0        1
    sdrpp_prf("sdrpp_accept send SpyServerDeviceInfo=%d\n", sizeof(di));
    send(sock_fd, &di, sizeof(di), 0);

// hf+
//0000: a406 0002 0100 0000 0000 0000 0000 0000 2400 0000 0100 0000 0000 0000 1009 0500  ................$...............
//0020: 1009 0500 1009 0500 4442 0100 bcae 5265 1009 0500 f0e7 4e65                      ........DB....Re......Ne

// rtl 75k
//0000: a406 0002 0100 0000 0000 0000 0000 0000 2400 0000 0100 0000 0000 0000 00e1 f505  ................$...............
//0020: 00e1 f505 00e1 f505 12b0 6e01 ee57 496b 4078 7d01 c08f 3a6b                      ..........n..WIk@x}...:k

// rtl 150k
//0000: a406 0002 0100 0000 0000 0000 0000 0000 2400 0000 0100 0000 0000 0000 00e1 f505  ................$...............
//0020: 00e1 f505 00e1 f505 242a 6f01 dcdd 486b 4078 7d01 c08f 3a6b                      ........$*o...Hk@x}...:k
    struct SpyServerClientSync cs;
    cs.mh.ProtocolID = SPYSERVER_PROTOCOL_VERSION;
    cs.mh.MessageType = SPYSERVER_MSG_TYPE_CLIENT_SYNC;
    cs.mh.StreamType = SPYSERVER_STREAM_TYPE_STATUS;
    cs.mh.SequenceNumber = 0;
    cs.mh.BodySize = CS_SIZE;    
    
                                // 0x24 9*4=36  // rtl      hf+
    cs.CanControl = 1;                            // 1        1
    cs.Gain = 0;                                  // 0        0
    cs.DeviceCenterFrequency = freq;              // 100M     330k
    cs.IQCenterFrequency = freq;                  // 100M     330k
    cs.FFTCenterFrequency = freq;                 // 100M     330k
    cs.MinimumIQCenterFrequency = f_min;          // 25M      82.5k
                                                // 24.031250(75k)
                                                // 24.062500(150k)
    cs.MaximumIQCenterFrequency = f_max;          // 1799M    1700M-82.5k
                                                // 1799.968750(75k)
                                                // 1799.937500(150k)
    cs.MinimumFFTCenterFrequency = f_min;         // 25M      330k
    cs.MaximumFFTCenterFrequency = f_max;          // 1799M    1700M-82.5k
    sdrpp_prf("sdrpp_accept send SpyServerClientSync=%d\n", sizeof(cs));
    send(sock_fd, &cs, sizeof(cs), 0);
}

// client => server
void sdrpp_in(int sock_fd)
{
    u1_t buf[1024];
    int bytes_in = recv(sock_fd, buf, sizeof(buf), 0);
    if (bytes_in <= 0) {
        return;  // Connection closed or error
    }
    int bytes_rem = bytes_in;
    u1_t *buf_ptr = buf;
    
    while (bytes_rem > 0) {
        // Check if we have enough bytes for the command header
        if (bytes_rem < (int)sizeof(struct SpyServerCommandHeader)) {
            sdrpp_prf("sdrpp_in: Not enough bytes for command header\n");
            break;
        }
        
        struct SpyServerCommandHeader *ch = (struct SpyServerCommandHeader *) buf_ptr;
        
        switch (ch->CommandType) {
        
//0000: 0000 0000 0900 0000 a406 0002 5344 522b 2b ............SDR++
        case SPYSERVER_CMD_HELLO: {
            // Check if we have enough bytes for the handshake
            if (bytes_rem < (int)sizeof(struct SpyServerClientHandshake)) {
                sdrpp_prf("sdrpp_in: Not enough bytes for handshake\n");
                bytes_rem = 0;
                break;
            }
            
            struct SpyServerClientHandshake *hs = (struct SpyServerClientHandshake *) buf_ptr;
            int len = bytes_rem - HS_SIZE;
            if (len > 0) {
                sdrpp_prf("SPYSERVER_CMD_HELLO ProtocolVersion=%08x(%08x) id=%.*s\n", hs->ProtocolVersion, SPYSERVER_PROTOCOL_VERSION, len, hs->id);
            } else {
                sdrpp_prf("SPYSERVER_CMD_HELLO ProtocolVersion=%08x(%08x)\n", hs->ProtocolVersion, SPYSERVER_PROTOCOL_VERSION);
            }
            bytes_rem = 0;
            break;
        }

// hf+ ???
//0000: 0200 0000 0800 0000 // 6400 0000 0200 0000  ........d.......
    
//0000: 0200 0000 0800 0000 // 6600 0000 0200 0000 // 0200 0000 0800 0000 // 6500 0000 3c37 0000  ........f...............e...<7..
//0020: 0200 0000 0800 0000 // 0000 0000 0100 0000 // 0200 0000 0800 0000 // 0200 0000 0000 0000  ................................
//0040: 0200 0000 0800 0000 // 6700 0000 0600 0000 // 0200 0000 0800 0000 // 0100 0000 0100 0000  ........g.......................
//0060: 0200 0000 0800 0000 // 6500 0000 3c37 0000                                          ........e...<7..
        case SPYSERVER_CMD_SET_SETTING: {
            // Check if we have enough bytes for the setting target
            if (bytes_rem < (int)sizeof(struct SpyServerSettingTarget)) {
                sdrpp_prf("sdrpp_in: Not enough bytes for setting target\n");
                bytes_rem = 0;
                break;
            }
            
            struct SpyServerSettingTarget *st = (struct SpyServerSettingTarget *) buf_ptr;
            //sdrpp_prf("SPYSERVER_CMD_SET_SETTING Setting=%d Value=%d\n", st->Setting, st->Value);
    
            switch (st->Setting) {
                case SPYSERVER_SETTING_STREAMING_MODE: sdrpp_prf("SPYSERVER_SETTING_STREAMING_MODE = %d\n", st->Value); break;
                case SPYSERVER_SETTING_STREAMING_ENABLED: sdrpp_prf("SPYSERVER_SETTING_STREAMING_ENABLED = %d\n", st->Value);
                    streaming_enabled = st->Value;
                    if (sdrpp_WR_tid != -1) {
                        #ifdef CAMP
                        #else
                            rx_chan_t *rxc = &rx_channels[0];
                            // rxc->wb_task = sdrpp_WR_tid;
                        #endif
                        TaskWakeup(sdrpp_WR_tid);
                    }
                    break;
                case SPYSERVER_SETTING_GAIN: sdrpp_prf("SPYSERVER_SETTING_GAIN = %d\n", st->Value); break;
    
                case SPYSERVER_SETTING_IQ_FORMAT: sdrpp_prf("SPYSERVER_SETTING_IQ_FORMAT = %d\n", st->Value); break;
                case SPYSERVER_SETTING_IQ_FREQUENCY:
                    //sdrpp_prf("SPYSERVER_SETTING_IQ_FREQUENCY = %d\n", st->Value);
                    // rx_sound_set_freq(NULL, (double) st->Value / 1e3, /* jksxmg FIXME */ false);
                    break;
                case SPYSERVER_SETTING_IQ_DECIMATION: sdrpp_prf("SPYSERVER_SETTING_IQ_DECIMATION = %d\n", st->Value); break;
                case SPYSERVER_SETTING_IQ_DIGITAL_GAIN: sdrpp_prf("SPYSERVER_SETTING_IQ_DIGITAL_GAIN = %d\n", st->Value); break;
    
                case SPYSERVER_SETTING_FFT_FORMAT: sdrpp_prf("SPYSERVER_SETTING_FFT_FORMAT = %d\n", st->Value); break;
                case SPYSERVER_SETTING_FFT_FREQUENCY: sdrpp_prf("SPYSERVER_SETTING_FFT_FREQUENCY = %d\n", st->Value); break;
                case SPYSERVER_SETTING_FFT_DECIMATION: sdrpp_prf("SPYSERVER_SETTING_FFT_DECIMATION = %d\n", st->Value); break;
                case SPYSERVER_SETTING_FFT_DB_OFFSET: sdrpp_prf("SPYSERVER_SETTING_FFT_DB_OFFSET = %d\n", st->Value); break;
                case SPYSERVER_SETTING_FFT_DB_RANGE: sdrpp_prf("SPYSERVER_SETTING_FFT_DB_RANGE = %d\n", st->Value); break;
                case SPYSERVER_SETTING_FFT_DISPLAY_PIXELS: sdrpp_prf("SPYSERVER_SETTING_FFT_DISPLAY_PIXELS = %d\n", st->Value); break;
    
                default: sdrpp_prf("SPYSERVER_CMD_SET_SETTING UNKNOWN Setting=%d Value=%d\n", st->Setting, st->Value); break;
            }
            
            // Move buffer pointer and update remaining bytes
            int cmd_size = sizeof(struct SpyServerSettingTarget);
            if (bytes_rem >= cmd_size) {
                buf_ptr += cmd_size;
                bytes_rem -= cmd_size;
            } else {
                bytes_rem = 0; // Not enough bytes, exit loop
            }
            break;
        }
        
        case SPYSERVER_CMD_PING: {
            sdrpp_prf("SPYSERVER_CMD_PING\n");
            bytes_rem = 0;
            break;
        }
        
        default:
            // sdr# ?
            // sdrpp_in: CommandType=1314410051 UNKNOWN
            // sdrpp_in: CommandType=1313165391 UNKNOWN
            sdrpp_prf("sdrpp_in: CommandType=%d UNKNOWN\n", ch->CommandType);
            bytes_rem = 0;
            break;
        }
    }
}

// server => client

// hf+
//0000: a406 0002 6400 0600 0100 0000 0000 0000 000f 0000 // 8080 8080 8080 8080 8080 8080  ....d...........................

// rtl 64|0f 75k 0x5c0 1472 736*2
//0020:                                                             a406 0002 6400 0f00  $}........n..WIk@x}...:k....d...
//0040: 0100 0000 0000 0000 c005 0000 8080 8080 8080 8080 8080 8080 8080 8080 8080 8080  ................................

// rtl 64|0c 150k 0xb80 2944 
//                                                                  a406 0002 6400 0c00  $}......$*o...Hk@x}...:k....d...
//0040: 0100 0000 0000 0000 800b 0000 8080 8080 8080 8080 8080 8080 8080 8080 8080 8080  ................................
//      strm ty iq      seq len

typedef struct {
    struct SpyServerMessageHeader mh;
    iq16_t buf16[N_SAMPS];
} out16_t;
static out16_t out16;

typedef struct {
    struct SpyServerMessageHeader mh;
    iq8_t buf8[N_SAMPS];
} out8_t;
static out8_t out8;

void sdrpp_out(int sock_fd, void *ev_data)
{
    
    #ifdef PACER
        static bool init;
        static u4_t start, tick;
        static double interval_us, elapsed_us;
        static int perHz;
        if (!init) {
            start = timer_us();
            init = true;
            elapsed_us = 0;
            interval_us = 1e6 / (float) bw * N_SAMPS;
            perHz = (int) round((float) bw / N_SAMPS);
            printf("sdrpp_out bw=%d interval(msec)=%.3f\n", bw, interval_us/1e3);
        }
        
        u4_t now = timer_us();
        u4_t diff = now - start;
        //real_printf("%d %.0f\n", diff, round(elapsed_us));
        if ((now - start) < round(elapsed_us)) {
            return;
        }
        if (tick > 1) elapsed_us += interval_us;
        tick++;
        //if ((tick % perHz) == 0) { real_printf("#"); fflush(stdout); }
        
        #ifdef FILE_IN
            if ((tick % perHz) == 0) { printf("%d\n", tick); }
        #endif
        #ifdef SOURCE_MMAP
            if ((tick % perHz) == 0) { printf("%d %p|%p\n", tick, in, fp + (fsize / (sizeof(iq16_t)))); }
        #endif
    #else
        #ifdef DIRECT
            rx_chan_t *rxc = &rx_channels[0];
            #ifdef CAMP
                if (!rxc->busy) {
                    //real_printf("."); fflush(stdout);
                    TaskSleepMsec(250);
                    return;
                }
            #endif
            
            rx_dpump_t *rx = &rx_dpump[0];
            while (rx->wr_pos == RD_POS) {
                TaskSleepReason("check pointers");
            }
            //real_printf("#"); fflush(stdout);
        #endif
    #endif
    
    #ifdef IQ16
        out16_t *out = &out16;
    #else
        out8_t *out = &out8;
    #endif

    static u4_t seq;
    #define GAIN 0xf
    out->mh.ProtocolID = SPYSERVER_PROTOCOL_VERSION;
    #ifdef IQ16
        out->mh.MessageType = (GAIN << 16) | SPYSERVER_MSG_TYPE_INT16_IQ;
    #else
        out->mh.MessageType = (GAIN << 16) | SPYSERVER_MSG_TYPE_UINT8_IQ;
    #endif
    out->mh.StreamType = SPYSERVER_STREAM_TYPE_IQ;
    out->mh.SequenceNumber = seq++;

    u4_t num_samps, out_bytes;
    int n;
    
    #ifdef FILE_IN
        num_samps = N_SAMPS;
        do {
            n = read(fd, &file, sizeof(file));
            if (n < sizeof(file)) lseek(fd, 0, SEEK_SET);
        } while (n < sizeof(file));
        in = file;
    #endif
    
    #ifdef SOURCE_MMAP
        num_samps = N_SAMPS;
        if ((in + N_SAMPS) >= (fp + (fsize / (sizeof(iq16_t))))) {
            in = fp;
        }
    #endif
    
    #ifdef DIRECT
        num_samps = nrx_samps;
        TYPECPX *in_wb_samps = rx->in_samps[RD_POS];
        //real_printf("<%d=%p> ", RD_POS, in_wb_samps); fflush(stdout);
        if (pos_wrap_diff(rx->wr_pos, RD_POS, N_DPBUF) >= N_DPBUF) {
            real_printf("X"); fflush(stdout);
        }
        RD_POS = (RD_POS+1) & (N_DPBUF-1);
        // rxc->rd++;
        //real_printf("."); fflush(stdout);
        //real_printf("%d ", num_samps); fflush(stdout);
    #endif
    
    out_bytes = num_samps * SIZEOF_SAMP;
    out->mh.BodySize = out_bytes;
    out_bytes += sizeof(struct SpyServerMessageHeader);

    #ifdef IQ16
        for (int j = 0; j < num_samps; j++, in++) {
            #ifdef DIRECT

                // zoom in fully to see the picket
                //#define PICKET
                #ifdef PICKET
                    u2_t i, q;
                    static u4_t ctr;
                    if ((ctr % 1024) == 0) i = 0xfff; else i = 0;
                    ctr++;
                    q = 0;
                #else
                    #if 1
                        u2_t i = (u2_t)(in_wb_samps[j].re * 16384.0f) & 0xffff;
                        u2_t q = (u2_t)(in_wb_samps[j].im * 16384.0f) & 0xffff;
                    #else
                        u2_t i = (u2_t)(in_wb_samps[j].re * 65536.0f) & 0xffff;
                        u2_t q = (u2_t)(in_wb_samps[j].im * 65536.0f) & 0xffff;
                    #endif
                #endif
            #else
                u2_t i = in->i;
                u2_t q = in->q;
            #endif
            
            #ifdef FLIP_IQ
                i = FLIP16(i);
                q = FLIP16(q);
            #endif
            
            #ifdef SWAP_IQ
                out->buf16[j].i = q;
                out->buf16[j].q = i;
            #else
                out->buf16[j].i = i;
                out->buf16[j].q = q;
            #endif
        }
        send(sock_fd, &out16, out_bytes, 0);
    #else
        for (int j = 0; j < num_samps; j++, in++) {
            u1_t i = in->i & 0xff;
            u1_t q = in->q & 0xff;
            
            #ifdef SWAP_IQ
                out->buf8[j].i = q;
                out->buf8[j].q = i;
            #else
                out->buf8[j].i = i;
                out->buf8[j].q = q;
            #endif
        }
        send(sock_fd, &out8, out_bytes, 0);
    #endif

}

static int server_sock_fd = -1;
static int client_sock_fd = -1;

static void handle_client_connection(void *param)
{
    int sock_fd = (int) FROM_VOID_PARAM(param);
    sdrpp_dprf("New client connection accepted\n");
    
    // Send initial device info and client sync
    sdrpp_accept(sock_fd);
    
    while (1) {
        // Handle incoming data
        sdrpp_in(sock_fd);
        
        // Check for socket errors or closure
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error != 0) {
            break;
        }
        
        NextTask("sdrpp_client");
    }
    
    sdrpp_dprf("Client connection closed\n");
    
    #ifdef DIRECT
        #ifdef CAMP
        #else
            rx_chan_t *rxc = &rx_channels[0];
            // rxc->wb_task = 0;
            rx_enable(0, RX_CHAN_FREE);
        #endif
    #endif

    if (sdrpp_WR_tid != -1)
        TaskRemove(sdrpp_WR_tid);
    sdrpp_WR_tid = -1;
    
    #if defined(FILE_IN) && !defined(SOURCE_MMAP)
        if (fd != -1) {
            close(fd);
            fd = -1;
        }
    #endif
    
    close(sock_fd);
}

static void sdrpp_server_listen(void *param)
{
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Create TCP socket
    server_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock_fd < 0) {
        lprintf("sdrpp_server: socket creation failed\n");
        return;
    }
    
    // Allow socket reuse
    int opt = 1;
    setsockopt(server_sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind to port 5555
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(5555);
    
    if (bind(server_sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        lprintf("sdrpp_server: bind failed\n");
        close(server_sock_fd);
        server_sock_fd = -1;
        return;
    }
    
    // Listen for connections
    if (listen(server_sock_fd, 5) < 0) {
        lprintf("sdrpp_server: listen failed\n");
        close(server_sock_fd);
        server_sock_fd = -1;
        return;
    }
    
    lprintf("sdrpp_server: listening on port 5555\n");
    
    while (1) {
        client_sock_fd = accept(server_sock_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock_fd < 0) {
            if (errno != EINTR) {
                lprintf("sdrpp_server: accept failed\n");
            }
            continue;
        }
        
        // Handle client connection in a separate task
        CreateTask(handle_client_connection, TO_VOID_PARAM(client_sock_fd), SND_PRIORITY);
        
        NextTask("sdrpp_server");
    }
}

void sdrpp_server_init(void)
{
    CreateTask(sdrpp_server_listen, NULL, SND_PRIORITY);
}
