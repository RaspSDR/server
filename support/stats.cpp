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

// Copyright (c) 2014-2016 John Seamons, ZL4VO/KF6VO

#include "types.h"
#include "config.h"
#include "options.h"
#include "kiwi.h"
#include "rx.h"
#include "rx_util.h"
#include "mem.h"
#include "misc.h"
#include "web.h"
#include "gps_.h"
#include "coroutines.h"
#include "debug.h"
#include "printf.h"
#include "non_block.h"
#include "eeprom.h"
#include "shmem.h"
#include "mqttpub.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

int current_nusers;
static int last_hour = -1, last_min = -1;

// called periodically (currently every 10 seconds)
static void webserver_collect_print_stats(int print) {
    int i, nusers = 0;
    conn_t* c;

    // print / log connections
    rx_chan_t* rx;
    for (rx = rx_channels; rx < &rx_channels[rx_chans]; rx++) {
        if (!rx->busy) continue;
        c = rx->conn;
        if (c == NULL || !c->valid) continue;
        // assert(c->type == STREAM_SOUND || c->type == STREAM_WATERFALL);

        u4_t now = timer_sec();
        if (c->freqHz != c->last_freqHz || c->mode != c->last_mode || c->zoom != c->last_zoom) {
            if (print) rx_loguser(c, LOG_UPDATE);

#ifdef OPTION_LOG_WF_ONLY_UPDATES
            if (c->type == STREAM_WATERFALL && c->last_log_time != 0) {
                rx_loguser(c, LOG_UPDATE);
            }
#endif

            c->last_tune_time = now;
            c->last_freqHz = c->freqHz;
            c->last_mode = c->mode;
            c->last_zoom = c->zoom;
            c->last_log_time = now;
        }
        else {
            u4_t diff = now - c->last_log_time;
            if (diff > MINUTES_TO_SEC(5)) {
                if (print) rx_loguser(c, LOG_UPDATE_NC);
            }

            // cprintf(c, "TO_MINS=%d exempt=%d\n", inactivity_timeout_mins, c->tlimit_exempt);
            if (inactivity_timeout_mins != 0 && !c->tlimit_exempt) {
                if (c->last_tune_time == 0) c->last_tune_time = now; // got here before first set in rx_loguser()
                diff = now - c->last_tune_time;
                // cprintf(c, "diff=%d now=%d last=%d TO_SECS=%d\n", diff, now, c->last_tune_time,
                //     MINUTES_TO_SEC(inactivity_timeout_mins));
                if (diff > MINUTES_TO_SEC(inactivity_timeout_mins)) {
                    cprintf(c, "TLIMIT-INACTIVE for %s\n", c->remote_ip);
                    send_msg(c, false, "MSG inactivity_timeout=%d", inactivity_timeout_mins);
                    c->inactivity_timeout = true;
                }
            }
        }

        if (ip_limit_mins && !c->tlimit_exempt) {
            if (c->tlimit_zombie) {
                // After the browser displays the "time limit reached" error panel the connection
                // hangs open until the watchdog goes off. So have to flag as a zombie to keep the
                // database from getting incorrectly updated.
                // cprintf(c, "TLIMIT-IP zombie %s\n", c->remote_ip);
            }
            else {
                int ipl_cur_secs = json_default_int(&cfg_ipl, c->remote_ip, 0, NULL);
                ipl_cur_secs += STATS_INTERVAL_SECS;
                // cprintf(c, "TLIMIT-IP setting database sec:%d for %s\n", ipl_cur_secs, c->remote_ip);
                json_set_int(&cfg_ipl, c->remote_ip, ipl_cur_secs);
                if (ipl_cur_secs >= MINUTES_TO_SEC(ip_limit_mins)) {
                    cprintf(c, "TLIMIT-IP connected 24hr LIMIT REACHED cur:%d >= lim:%d for %s\n",
                            SEC_TO_MINUTES(ipl_cur_secs), ip_limit_mins, c->remote_ip);
                    send_msg_encoded(c, "MSG", "ip_limit", "%d,%s", ip_limit_mins, c->remote_ip);
                    c->inactivity_timeout = true;
                    c->tlimit_zombie = true;
                }
            }
        }

#ifdef OPTION_SERVER_GEOLOC
        if (!c->geo && !c->try_geoloc && (now - c->arrival) > 10) {
            // clprintf(c, "GEOLOC: %s sent no geoloc info, trying from here\n", c->remote_ip);
            CreateTask(geoloc_task, (void*)c, SERVICES_PRIORITY);
            c->try_geoloc = true;
        }
#endif

// SND and/or WF connections that have failed to follow API
#define NO_API_TIME 20
        bool both_no_api = (!c->snd_cmd_recv_ok && !c->wf_cmd_recv_ok);
        if (c->auth && both_no_api && (now - c->arrival) >= NO_API_TIME) {
            clprintf(c, "\"%s\"%s%s%s%s incomplete connection kicked\n",
                     c->ident_user ? c->ident_user : "(no identity)", c->isUserIP ? "" : " ", c->isUserIP ? "" : c->remote_ip,
                     c->geo ? " " : "", c->geo ? c->geo : "");
            c->kick = true;
        }

        nusers++;
    }
    current_nusers = nusers;

// construct cpu stats response
#define NCPU 4
    int usi[3][NCPU], del_usi[3][NCPU];
    static int last_usi[3][NCPU];

    u4_t now = timer_ms();
    static u4_t last_now;
    float secs = (float)(now - last_now) / 1000;
    last_now = now;

    char* reply = read_file_string_reply("/proc/stat");

    if (reply != NULL) {
        int ncpu;
        char* cpu_ptr = kstr_sp(reply);
        for (ncpu = 0; ncpu < NCPU; ncpu++) {
            char buf[10];
            kiwi_snprintf_buf(buf, "cpu%d", ncpu);
            cpu_ptr = strstr(cpu_ptr, buf);
            if (cpu_ptr == nullptr)
                break;
            sscanf(cpu_ptr + 4, " %d %*d %d %d", &usi[0][ncpu], &usi[1][ncpu], &usi[2][ncpu]);
        }

        for (i = 0; i < ncpu; i++) {
            del_usi[0][i] = lroundf((float)(usi[0][i] - last_usi[0][i]) / secs);
            del_usi[1][i] = lroundf((float)(usi[1][i] - last_usi[1][i]) / secs);
            del_usi[2][i] = lroundf((float)(usi[2][i] - last_usi[2][i]) / secs);
            // printf("CPU%d %.1fs u=%d%% s=%d%% i=%d%%\n", i, secs, del_usi[0][i], del_usi[1][i], del_usi[2][i]);
        }

        kstr_free(reply);
        int cpufreq_kHz = 0;
        float temp_deg_mC = 0;

        // find out tempture
        const char* raw_path = "/sys/bus/iio/devices/iio:device0/in_temp0_raw";
        const char* offset_path = "/sys/bus/iio/devices/iio:device0/in_temp0_offset";
        const char* scale_path = "/sys/bus/iio/devices/iio:device0/in_temp0_scale";
        static float t_scale = 0.0f, t_offset = 0.0f;
        if (t_scale == 0.0f) {
            kstr_t* reply = read_file_string_reply(scale_path);
            sscanf(kstr_sp(reply), "%f", &t_scale);
            kstr_free(reply);
            reply = read_file_string_reply(offset_path);
            sscanf(kstr_sp(reply), "%f", &t_offset);
            kstr_free(reply);
            // printf("Tempture scale: %f offset: %f\n", t_scale, t_offset);
        }
        float raw_value;
        kstr_t* reply = read_file_string_reply(raw_path);
        sscanf(kstr_sp(reply), "%f", &raw_value);
        kstr_free(reply);
        temp_deg_mC = (raw_value + t_offset) * t_scale / 1000 - 10.0;
        // printf("Tempture scale: %f offset: %f raw: %f => Temp=%d\n", t_scale, t_offset, raw_value, temp_deg_mC);

        // find out cpu freq
        const char* cpufreq_path = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq";
        {
            kstr_t* reply = read_file_string_reply(cpufreq_path);
            sscanf(kstr_sp(reply), "%u", &cpufreq_kHz);
            kstr_free(reply);
        }

        // ecpu_use() below can thread block, so cpu_stats_buf must be properly set NULL for reading thread
        kstr_t* ks;
        if (cpu_stats_buf) {
            ks = cpu_stats_buf;
            cpu_stats_buf = NULL;
            kstr_free(ks);
        }
        ks = kstr_asprintf(NULL, "\"ct\":%d,\"cf\":%f,\"cc\":%.0f,",
                           timer_sec(), cpufreq_kHz / 1000.0, temp_deg_mC);

        ks = kstr_cat(ks, kstr_list_int("\"cu\":[", "%d", "],", &del_usi[0][0], ncpu));
        ks = kstr_cat(ks, kstr_list_int("\"cs\":[", "%d", "],", &del_usi[1][0], ncpu));
        ks = kstr_cat(ks, kstr_list_int("\"ci\":[", "%d", "]", &del_usi[2][0], ncpu));

        mqtt_publish("stat", "\"temp\":%.0f, \"cpu\": %.0f", temp_deg_mC, (del_usi[0][0] + del_usi[0][1]) * 0.5f);

        for (i = 0; i < ncpu; i++) {
            last_usi[0][i] = usi[0][i];
            last_usi[1][i] = usi[1][i];
            last_usi[2][i] = usi[2][i];
        }

        cpu_stats_buf = ks;
    }

    // collect network i/o stats
    static const float k = 1.0 / 1000.0 / 10.0; // kbytes/sec every 10 secs

    for (i = 0; i <= rx_chans; i++) { // [rx_chans] is total of all channels
        audio_kbps[i] = audio_bytes[i] * k;
        waterfall_kbps[i] = waterfall_bytes[i] * k;
        waterfall_fps[i] = waterfall_frames[i] / 10.0;
        audio_bytes[i] = waterfall_bytes[i] = waterfall_frames[i] = 0;
    }
    http_kbps = http_bytes * k;
    http_bytes = 0;

    // on the hour: report number of connected users & schedule updates
    int hour, min;
    utc_hour_min_sec(&hour, &min);

    if (hour != last_hour) {
        if (print) lprintf("(%d %s)\n", nusers, (nusers == 1) ? "user" : "users");
        last_hour = hour;
    }

    // call every minute at the top of the minute
    if (min != last_min) {
        schedule_update(min);
        last_min = min;
    }

    rx_autorun_restart_victims(false);
}

static int CAT_task_tid;
static int CAT_last_freqHz;

static void called_every_second() {
    conn_t* c;
    u4_t now = timer_sec();
    int ch;
    rx_chan_t* rx;

    for (rx = rx_channels, ch = 0; rx < &rx_channels[rx_chans]; rx++, ch++) {


        // CAT tuned frequency reporting

        if (!rx->busy) {
            if (kiwi.CAT_ch == ch) {
                kiwi.CAT_ch = -1;
                return;
            }
            continue;
        }
        c = rx->conn;
        if (c == NULL || !c->valid) {
            if (kiwi.CAT_ch == ch) {
                kiwi.CAT_ch = -1;
                return;
            }
            continue;
        }

        if (kiwi.CAT_fd > 0 && !c->internal_connection && (kiwi.CAT_ch < 0 || kiwi.CAT_ch == ch)) {
            // printf("CAT_CH=%d ch=%d\n", kiwi.CAT_ch, ch);
            if (kiwi.CAT_ch < 0) kiwi.CAT_ch = ch; // lock to the first channel encountered

            if (c->freqHz != CAT_last_freqHz) {
                CAT_last_freqHz = c->freqHz;
                TaskWakeup(CAT_task_tid);
            }
        }


        // External API detection and limiting

        if (c->ext_api_determined) continue;
        int served = web_served(c);

#ifdef OPTION_HONEY_POT
        cprintf(c, "API: rx%d arrival=%d served=%d type=%s ext_api%d isLocal%d internal%d\n",
                ch, now - c->arrival, served, rx_conn_type(c),
                c->ext_api, c->isLocal, c->internal_connection);
#endif

        if (c->isLocal || c->internal_connection || c->awaitingPassword ||
            (c->type != STREAM_SOUND && c->type != STREAM_WATERFALL)) {
            c->ext_api = false;
            c->ext_api_determined = true;
            // cprintf(c, "API: connection is exempt\n");
            continue;
        }

// can't be too short because of slow network connections delaying the served counter
#define EXT_API_DECISION_SECS 10
        if ((now - c->arrival) < EXT_API_DECISION_SECS) continue;

// can't be too many due to browser caching (lowest limit currently seen with iOS caching)
#define EXT_API_DECISION_SERVED 3
        if (served >= EXT_API_DECISION_SERVED) {
            c->ext_api = false;
            c->ext_api_determined = true;
            web_served_clear_cache(c);
            cprintf(c, "API: decided connection is OKAY (%d) %s\n",
                    served, c->browser ? c->browser : "");
            continue;
        }

        c->ext_api = c->ext_api_determined = true;
        web_served_clear_cache(c);
        cprintf(c, "API: decided connection is non-Kiwi app (%d) %s\n",
                served, c->browser ? c->browser : "");
        // dump();

        // ext_api_nchans, if exceeded, overrides tdoa_nchans
        if (!c->kick && !c->awaitingPassword) {
            int ext_api_ch = cfg_int("ext_api_nchans", NULL, CFG_REQUIRED);
            if (ext_api_ch == -1) ext_api_ch = rx_chans; // has never been set
            int ext_api_users = rx_count_server_conns(EXT_API_USERS);
            cprintf(c, "API: ext_api_users=%d >? ext_api_ch=%d %s\n", ext_api_users, ext_api_ch,
                    (ext_api_users > ext_api_ch) ? "T(DENY)" : "F(OKAY)");
            bool kick = false;
            if (ext_api_users > ext_api_ch) {
                clprintf(c, "API: non-Kiwi app was denied connection: %d/%d %s \"%s\"\n",
                         ext_api_users, ext_api_ch, c->remote_ip, c->ident_user);
                kick = true;
            }
            else {
#ifdef OPTION_DENY_APP_FINGERPRINT_CONN
                float f_kHz = (float)c->freqHz / kHz + freq_offset_kHz;
                // 58.59 175.781
                float floor_kHz = floorf(f_kHz);
                bool freq_trig = (floor_kHz == 58.0f || floor_kHz == 175.0f);
                bool hasDelimiter = (c->ident_user != NULL && strpbrk(c->ident_user, "-_.`,/+~") != NULL);
                // bool trig = (c->type == STREAM_WATERFALL && freq_trig && hasDelimiter && c->zoom == 8);
                bool trig = (c->type == STREAM_WATERFALL && freq_trig && c->zoom == 8);
                clprintf(c, "API: TRIG=%d %s(%d) f_kHz=%.3f freq_trig=%d hasDelimiter=%d z=%d\n",
                         trig, rx_conn_type(c), c->type, f_kHz, freq_trig, hasDelimiter, c->zoom);
                if (trig) {
                    clprintf(c, "API: non-Kiwi app fingerprint was denied connection\n");
                    kick = true;
                }
#endif
            }

            if (kick) {
                send_msg(c, SM_NO_DEBUG, "MSG too_busy=%d", ext_api_ch);
                c->kick = true;
            }
        }


        // TDoA connection detection and limiting
        //
        // Can only distinguish the TDoA service at the time the kiwirecorder identifies itself.
        // If a match and the limit is exceeded then kick the connection off immediately.
        // This identification is typically sent right after initial connection is made.

        if (!c->kick && c->ident_user && kiwi_str_begins_with(c->ident_user, "TDoA_service")) {
            int tdoa_ch = cfg_int("tdoa_nchans", NULL, CFG_REQUIRED);
            if (tdoa_ch == -1) tdoa_ch = rx_chans; // has never been set
            int tdoa_users = rx_count_server_conns(TDOA_USERS);
            cprintf(c, "TDoA_service tdoa_users=%d >? tdoa_ch=%d %s\n", tdoa_users, tdoa_ch, (tdoa_users > tdoa_ch) ? "T(DENY)" : "F(OKAY)");
            if (tdoa_users > tdoa_ch) {
                send_msg(c, SM_NO_DEBUG, "MSG too_busy=%d", tdoa_ch);
                c->kick = true;
            }
        }
    }
}

static void CAT_write_tty(void* param) {
    int CAT_fd = (int)FROM_VOID_PARAM(param);

    static int last_freqHz;
    if (last_freqHz != CAT_last_freqHz) {
        char* s;
        // asprintf(&s, "IF%011d     +000000 000%1d%1d00001 ;\n", CAT_last_freqHz, /* mode */ 0, /* vfo */ 0);
        asprintf(&s, "FA%011d;\n", CAT_last_freqHz);
        // real_printf("CAT %s", s);
        write(CAT_fd, s, strlen(s));
        free(s);
        last_freqHz = CAT_last_freqHz;
    }
}

void CAT_task(void* param) {
    int CAT_fd = (int)FROM_VOID_PARAM(param);

    while (1) {
        TaskSleep();
        CAT_write_tty(TO_VOID_PARAM(CAT_fd));
    }
}

void stat_task(void* param) {
    u64_t stats_deadline = timer_us64() + SEC_TO_USEC(1);
    u64_t secs = 0;

    bool err;
    int baud = cfg_int("CAT_baud", &err, CFG_OPTIONAL);
    if (!err && baud > 0) {
        bool isUSB = true;
        kiwi.CAT_fd = open("/dev/ttyUSB0", O_RDWR | O_NONBLOCK);
        if (kiwi.CAT_fd < 0) {
            isUSB = false;
            kiwi.CAT_fd = open("/dev/ttyACM0", O_RDWR | O_NONBLOCK);
        }

        if (kiwi.CAT_fd >= 0) {
            kiwi.CAT_ch = -1;
            char* s;
            const int baud_i[] = { 0, 115200 };
            asprintf(&s, "stty -F /dev/tty%s0 %d", isUSB ? "USB" : "ACM", baud_i[baud]);
            system(s);
            free(s);
            printf("CAT /dev/tty%s0 %d baud\n", isUSB ? "USB" : "ACM", baud_i[baud]);
            // if (isUSB) system("stty -a -F /dev/ttyUSB0"); else system("stty -a -F /dev/ttyACM0");
            CAT_task_tid = CreateTask(CAT_task, TO_VOID_PARAM(kiwi.CAT_fd), CAT_PRIORITY);
        }
    }

    webserver_collect_print_stats(print_stats & STATS_TASK);

    while (TRUE) {
        called_every_second();

        if ((secs % STATS_INTERVAL_SECS) == 0) {
            webserver_collect_print_stats(print_stats & STATS_TASK);
            nbuf_stat();

            cull_zombies();
        }

        NextTask("stat task");

        // update on a regular interval
        u64_t now_us = timer_us64();
        s64_t diff = stats_deadline - now_us;
        if (diff > 0) TaskSleepUsec(diff);
        stats_deadline += SEC_TO_USEC(1);
        secs++;
    }
}
