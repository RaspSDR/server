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

// Copyright (c) 2016 John Seamons, ZL4VO/KF6VO

#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "mem.h"
#include "misc.h"
#include "str.h"
#include "timer.h"
#include "web.h"
#include "cfg.h"
#include "coroutines.h"
#include "net.h"
#include "rx.h"
#include "rx_util.h"
#include "services.h"
#include "peri.h"

#include <types.h>
#include <unistd.h>
#include <sys/stat.h>

#include <string>

static bool update_pending = false, update_task_running = false, update_in_progress = false;
static int pending_maj = -1, pending_min = -1;

static bool file_auto_download_check = false;
static bool file_auto_download_oneshot = false;

enum fail_reason_e {
    FAIL_NONE = 0,
    FAIL_FS_FULL = 1,
    FAIL_NO_INET = 2,
    FAIL_NO_GITHUB = 3,
    FAIL_GIT = 4,
    FAIL_MAKEFILE = 5,
    FAIL_BUILD = 6
};
fail_reason_e fail_reason;

static void report_result(conn_t* conn) {
    // let admin interface know result
    assert(conn != NULL);
    char* date_m = kiwi_str_encode((char*)__DATE__);
    char* time_m = kiwi_str_encode((char*)__TIME__);
    send_msg(conn, false, "MSG update_cb="
                          "{\"f\":%d,\"p\":%d,\"i\":%d,\"r\":%d,\"g\":%d,"
                          "\"v1\":%d,\"v2\":%d,\"p1\":%d,\"p2\":%d,\"d\":\"%s\",\"t\":\"%s\"}",
             fail_reason, update_pending, update_in_progress, rx_chans, gps_chans,
             version_maj, version_min, pending_maj, pending_min, date_m, time_m);
    kiwi_ifree(date_m, "date_m");
    kiwi_ifree(time_m, "time_m");
}

static void report_progress(conn_t* conn, const char* msg) {
    // let admin interface know result
    assert(conn != NULL);
    char* msg_m = kiwi_str_encode((char*)msg);
    send_msg(conn, false, "MSG update_cb={\"msg\":\"%s\"}\n", msg_m);

    kiwi_ifree(msg_m, "msg_m");
}

static int update_build(conn_t* conn, bool report, const char* channel, bool force_build) {
    bool no_fpga = false;
    std::string url_base = "https://downloads.rx-888.com/web-888/" + std::string(channel) + "/";
    sd_enable(true);

    // Fetch the binary
    int status = system("mkdir -p /media/mmcblk0p1/update; rm -Rf /media/mmcblk0p1/update/*");
    if (status != 0) {
        lprintf("UPDATE: create folder status=0x%08x\n", status);
        goto exit;
    }

    if (report) report_progress(conn, "Download FPGA firmware");

    status = curl_get_file((url_base + "websdr_hf.bit").c_str(), "/media/mmcblk0p1/update/websdr_hf.bit", 15);
    if (status != 0) {
        no_fpga = true;
    }

    if (report) report_progress(conn, "Download Web-888 Server");

    status = blocking_system((url_base + "websdr.bin").c_str(), "/media/mmcblk0p1/update/websdr.bin", 15);
    if (status != 0) {
        lprintf("UPDATE: fetch binary status=0x%08x\n", status);
        goto exit;
    }

    if (report) report_progress(conn, "Download checksum file");
    status = blocking_system((url_base + "checksum").c_str(), "/media/mmcblk0p1/update/checksum", 15);
    if (status != 0) {
        lprintf("UPDATE: fetch checksum status=0x%08x\n", status);
        goto exit;
    }

    if (report) report_progress(conn, "Checksum the downloaded files");
    status = system("cd /media/mmcblk0p1/update; sed '/web-888-alpine/d;/websdr_vhf/d' /media/mmcblk0p1/update/checksum | sha256sum -c -");
    if (status != 0) {
        lprintf("UPDATE: checksum failed status=0x%08x\n", status);
        if (report) report_progress(conn, "Checksum failed");

        goto exit;
    }

    if (report) report_progress(conn, "Install update to SD card");

    system("rm /media/mmcblk0p1/websdr.bin.old");
    system("mv /media/mmcblk0p1/websdr.bin /media/mmcblk0p1/websdr.bin.old; mv /media/mmcblk0p1/update/websdr.bin /media/mmcblk0p1/websdr.bin");

    if (!no_fpga) {
        system("rm /media/mmcblk0p1/websdr_hf.bit.old");
        system("mv /media/mmcblk0p1/websdr_hf.bit /media/mmcblk0p1/websdr_hf.bit.old; mv /media/mmcblk0p1/update/websdr_hf.bit /media/mmcblk0p1/websdr_hf.bit");
    }

    status = EXIT_SUCCESS;

exit:
    sd_enable(false);

    return status;
}

static void _update_task(void* param) {
    conn_t* conn = (conn_t*)FROM_VOID_PARAM(param);
    bool force_check = (conn && conn->update_check == FORCE_CHECK);
    bool force_build = (conn && conn->update_check == FORCE_BUILD);
    bool report = (force_check || force_build);
    bool ver_changed, update_install;
    int status;
    kstr_t* ver = NULL;
    fail_reason = FAIL_NONE;

    update_in_progress = true;

    bool err;
    bool ch = admcfg_bool("update_channel", &err, CFG_OPTIONAL);
    if (err) ch = false;

    lprintf("UPDATE: checking for updates\n");
    if (force_check) update_pending = false; // don't let pending status override version reporting when a forced check

    if (report) report_progress(conn, "Checking internet connectivity");
#define PING_INET "ping -qc2 1.1.1.1 >/dev/null 2>&1"
    status = non_blocking_cmd_system_child("kiwi.ck_inet", PING_INET, POLL_MSEC(250));
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {

#define PING_INET2 "ping -qc2 8.8.8.8 >/dev/null 2>&1"
        status = non_blocking_cmd_system_child("kiwi.ck_inet", PING_INET2, POLL_MSEC(250));
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            lprintf("UPDATE: No Internet connection? (can't ping 1.1.1.1 or 8.8.8.8)\n");
            fail_reason = FAIL_NO_INET;
            if (report) report_result(conn);
            goto common_return;
        }
    }

    if (report) report_progress(conn, "Getting latest version information");
    // get pending_maj, pending_min
    // get the latest version infor from www.rx-888.com
    // Run fetch in a Linux child process otherwise this thread will block and cause trouble
    // if the check is invoked from the admin page while there are active user connections.
    
    ver = curl_get(ch ? "https://downloads.rx-888.com/web-888/alpha/version.txt" : "https://downloads.rx-888.com/web-888/stable/version.txt", 5, &status);

    if (ver == NULL || status != 0) {
        lprintf("UPDATE: failed to get latest version information from server\n");
        fail_reason = FAIL_MAKEFILE;
        if (report) report_result(conn);
        goto common_return;
    }

    {
        int n = sscanf(kstr_sp(ver), "%d.%d", &pending_maj, &pending_min);

        ver_changed = (n == 2 && (pending_maj > version_maj || (pending_maj == version_maj && pending_min > version_min)));
        update_install = (admcfg_bool("update_install", NULL, CFG_REQUIRED) == true);
        kstr_free(ver);
    }

    {
        if (kiwi_file_exists("/root/config/force_update")) {
            ver_changed = true;
            update_install = true;
        }
    }

    if (force_check) {
        update_in_progress = false;
        if (ver_changed)
            lprintf("UPDATE: version changed (current %d.%d, new %d.%d), but check only\n",
                    version_maj, version_min, pending_maj, pending_min);
        else
            lprintf("UPDATE: running the most current version\n");

        if (report) report_result(conn);
        goto common_return;
    }
    else

        if (ver_changed && !update_install) {
        lprintf("UPDATE: version changed (current %d.%d, new %d.%d), but update install not enabled\n",
                version_maj, version_min, pending_maj, pending_min);
    }
    else

        if (ver_changed || force_build) {
        lprintf("UPDATE: version changed, current %d.%d, new %d.%d\n",
                version_maj, version_min, pending_maj, pending_min);
        lprintf("UPDATE: installing new version..\n");

        u4_t build_time = timer_sec();
        status = update_build(conn, report, ch ? "alpha" : "stable", force_build);

        if (status) {
            lprintf("UPDATE: Build failed, check /root/build.log file\n");
            fail_reason = FAIL_BUILD;
            if (force_build && report) report_result(conn);
            goto common_return;
        }

        lprintf("UPDATE: build took %d secs\n", timer_sec() - build_time);
        lprintf("UPDATE: switching to new version %d.%d\n", pending_maj, pending_min);

        lprintf("UPDATE: rebooting Beagle..\n");
        system("sleep 3; reboot");
    }
    else {
        lprintf("UPDATE: version %d.%d is current\n", version_maj, version_min);
    }

common_return:
    if (file_auto_download_oneshot) {
        file_auto_download_oneshot = false;
        // printf("file_GET: update check normal\n");
        file_GET(TO_VOID_PARAM(FILE_DOWNLOAD_DIFF_RESTART));
    }

    if (conn) conn->update_check = WAIT_UNTIL_NO_USERS; // restore default
    update_pending = update_task_running = update_in_progress = false;
}

// called at update check TOD, on each user logout in case update is pending or on demand by admin UI
void check_for_update(update_check_e type, conn_t* conn) {
    bool force = (type != WAIT_UNTIL_NO_USERS);

    if (!force && admcfg_bool("update_check", NULL, CFG_REQUIRED) == false) {
        // printf("UPDATE: exiting because admin update check not enabled\n");

        if (file_auto_download_check) {
            file_auto_download_check = false;
            // printf("file_GET: update check false\n");
            file_GET(TO_VOID_PARAM(FILE_DOWNLOAD_DIFF_RESTART));
        }

        return;
    }

    if (force) {
        lprintf("UPDATE: force %s by admin\n", (type == FORCE_CHECK) ? "update check" : "build");
        assert(conn != NULL);
        if (update_task_running) {
            lprintf("UPDATE: update task already running\n");
            report_result(conn);
            return;
        }
        else {
            conn->update_check = type;
        }
    }

    if (file_auto_download_check) {
        file_auto_download_oneshot = true;
        file_auto_download_check = false;
    }

    if ((force || (update_pending && rx_count_server_conns(EXTERNAL_ONLY) == 0)) && !update_task_running) {
        update_task_running = true;
        CreateTask(_update_task, TO_VOID_PARAM(conn), ADMIN_PRIORITY);
    }
}

static bool update_on_startup = true;
static int prev_update_window = -1;

// called at the top of each minute
void schedule_update(int min) {
#define UPDATE_SPREAD_HOURS 5 // # hours to spread updates over
#define UPDATE_SPREAD_MIN   (UPDATE_SPREAD_HOURS * 60)

#define UPDATE_START_HOUR 1 // 1 AM local time
#define UPDATE_END_HOUR   (UPDATE_START_HOUR + UPDATE_SPREAD_HOURS)

    // relative to local time if timezone has been determined, utc otherwise
    int local_hour;
    (void)local_hour_min_sec(&local_hour);

//#define TEST_UPDATE     // enables printf()s and simulates local time entering update window
#ifdef TEST_UPDATE
    int utc_hour;
    time_hour_min_sec(utc_time(), &utc_hour);
    printf("UPDATE: UTC=%02d:%02d Local=%02d:%02d update_window=[%02d:00,%02d:00]\n",
           utc_hour, min, local_hour, min, UPDATE_START_HOUR, UPDATE_END_HOUR);
    local_hour = 1;
#endif

    bool update_window = (local_hour >= UPDATE_START_HOUR && local_hour < UPDATE_END_HOUR);
    bool first_update_window = false;

    // don't let Kiwis hit github.com all at once!
    if (update_window) {
        int mins_now = min + (local_hour - UPDATE_START_HOUR) * 60;
        int serno = serial_number;

#ifdef TEST_UPDATE
#define SERNO_MIN_TRIG1 1 // set to minute in the future test update should start
#define SERNO_MIN_TRIG2 2 // set to minute in the future test update should start
        serno = (mins_now <= SERNO_MIN_TRIG1) ? SERNO_MIN_TRIG1 : SERNO_MIN_TRIG2;
        int mins_trig = serno % UPDATE_SPREAD_MIN;
        int hr_trig = UPDATE_START_HOUR + mins_trig / 60;
        int min_trig = mins_trig % 60;
        printf("TEST_UPDATE: %02d:%02d mins_now=%d mins_trig=%d (%02d:%02d sn=%d)\n",
               local_hour, min, mins_now, mins_trig, hr_trig, min_trig, serno);
#endif

        update_window = update_window && (mins_now == (serno % UPDATE_SPREAD_MIN));

        if (prev_update_window == -1) prev_update_window = update_window ? 1 : 0;
        first_update_window = ((prev_update_window == 0) && update_window);
#ifdef TEST_UPDATE
        printf("TEST_UPDATE: update_window=%d prev_update_window=%d first_update_window=%d\n",
               update_window, prev_update_window, first_update_window);
#endif
        prev_update_window = update_window ? 1 : 0;

        if (update_window) {
            printf("TLIMIT-IP 24hr cache cleared\n");
            json_release(&cfg_ipl);
            json_init(&cfg_ipl, (char*)"{}", "cfg_ipl"); // clear 24hr ip address connect time limit cache
        }
    }

//#define TRIG_UPDATE
#ifdef TRIG_UPDATE
    static bool trig_update;
    if (timer_sec() >= 60 && !trig_update) {
        update_window = true;
        trig_update = true;
    }
#endif

    file_auto_download_check = first_update_window && !update_on_startup;

    // printf("min=%d file_auto_download_check=%d update_window=%d update_on_startup=%d\n",
    //     timer_sec()/60, file_auto_download_check, update_window, update_on_startup);

    if (update_on_startup && admcfg_int("restart_update", NULL, CFG_REQUIRED) != 0) {
        lprintf("UPDATE: update on restart delayed until update window\n");
        update_on_startup = false;
    }

    if (update_window || update_on_startup) {
        lprintf("UPDATE: check scheduled %s\n", update_on_startup ? "(startup)" : "");
        update_on_startup = false;
        update_pending = true;
        check_for_update(WAIT_UNTIL_NO_USERS, NULL);
    }
}
