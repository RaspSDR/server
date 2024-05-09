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

bool update_pending = false, update_task_running = false, update_in_progress = false;
int pending_maj = -1, pending_min = -1;

static bool file_auto_download_check = false;
static bool file_auto_download_oneshot = false;
static bool do_daily_restart = false;

enum fail_reason_e {
    FAIL_NONE = 0,
    FAIL_FS_FULL = 1, FAIL_NO_INET = 2, FAIL_NO_GITHUB = 3, FAIL_GIT = 4,
    FAIL_MAKEFILE = 5, FAIL_BUILD = 6
};
fail_reason_e fail_reason;

static void report_result(conn_t *conn)
{
	// let admin interface know result
	assert(conn != NULL);
	char *date_m = kiwi_str_encode((char *) __DATE__);
	char *time_m = kiwi_str_encode((char *) __TIME__);
	send_msg(conn, false, "MSG update_cb="
	    "{\"f\":%d,\"p\":%d,\"i\":%d,\"r\":%d,\"g\":%d,"
	    "\"v1\":%d,\"v2\":%d,\"p1\":%d,\"p2\":%d,\"d\":\"%s\",\"t\":\"%s\"}",
		fail_reason, update_pending, update_in_progress, rx_chans, gps_chans,
		version_maj, version_min, pending_maj, pending_min, date_m, time_m);
	kiwi_ifree(date_m, "date_m");
	kiwi_ifree(time_m, "time_m");
}

static void update_build_ctask(void *param)
{
	sd_enable(true);

	// Fetch the binary
	int status = system("mkdir -p /media/mmcblk0p1/update; rm -Rf /media/mmcblk0p1/update/*");
	if (status != 0)
    {
	    printf("UPDATE: create folder status=0x%08x\n", status);
		goto exit;
	}

	status = system("curl -s -o /media/mmcblk0p1/update/sdr_receiver_kiwi.bit http://downloads.rx-888.com/web-888/sdr_receiver_kiwi.bit");
	if (status != 0)
    {
	    printf("UPDATE: fetch bistream status=0x%08x\n", status);
		goto exit;
	}

	status = system("curl -s -o /media/mmcblk0p1/update/kiwi.bin http://downloads.rx-888.com/web-888/kiwi.bin");
	if (status != 0)
    {
	    printf("UPDATE: fetch binary status=0x%08x\n", status);
		goto exit;
	}

	status = system("curl -s -o /media/mmcblk0p1/update/checksum http://downloads.rx-888.com/web-888/checksum");
	if (status != 0)
    {
	    printf("UPDATE: fetch checksum status=0x%08x\n", status);
		goto exit;
	}

	status = system("cd /media/mmcblk0p1/update; sha256sum -c /media/mmcblk0p1/update/checksum");
	if (status != 0)
    {
	    printf("UPDATE: checksum failed status=0x%08x\n", status);
		goto exit;
	}

	system("rm /media/mmcblk0p1/kiwi.bin.old");
	system("rm /media/mmcblk0p1/sdr_receiver_kiwi.bit.old");
	system("mv /media/mmcblk0p1/kiwi.bin /media/mmcblk0p1/kiwi.bin.old; mv /media/mmcblk0p1/update/kiwi.bin /media/mmcblk0p1/kiwi.bin");
	system("mv /media/mmcblk0p1/sdr_receiver_kiwi.bit /media/mmcblk0p1/sdr_receiver_kiwi.bit.old; mv /media/mmcblk0p1/update/sdr_receiver_kiwi.bit /media/mmcblk0p1/sdr_receiver_kiwi.bit");

	sd_enable(false);

	child_exit(EXIT_SUCCESS);

exit:
	child_status_exit(status);
}

static void fetch_makefile_ctask(void *param)
{
	// fetch the version info from server
	int status = system("curl -o /root/web-888.latest http://downloads.rx-888.com/web-888/version.txt");
	if (status != 0)
        printf("UPDATE: fetch origin status=0x%08x\n", status);
	child_status_exit(status);

	child_exit(EXIT_SUCCESS);
}

/*
    // task
    _update_task()
        status = child_task(fetch_makefile_ctask)
	    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	        error ...
        status = child_task(update_build_ctask)
	    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	        error ...

    child_task(func)
        if (fork())
            // child
            func() -> fetch_makefile_ctask() / update_build_ctask()
                status = system(...)
                if (status < 0)
                    child_exit(EXIT_FAILURE);
                if (WIFEXITED(status))
                    child_exit(WEXITSTATUS(status));
                child_exit(EXIT_FAILURE);
        // parent
        while
            waitpid(&status)
        return status
*/

static void _update_task(void *param)
{
	conn_t *conn = (conn_t *) FROM_VOID_PARAM(param);
	bool force_check = (conn && conn->update_check == FORCE_CHECK);
	bool force_build = (conn && conn->update_check == FORCE_BUILD);
	bool report = (force_check || force_build);
	bool ver_changed, update_install;
	int status;
	fail_reason = FAIL_NONE;
	
	lprintf("UPDATE: checking for updates\n");
	if (force_check) update_pending = false;    // don't let pending status override version reporting when a forced check
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

	// get pending_maj, pending_min
	// wget the latest version infor from www.rx-888.com
	// Run fetch in a Linux child process otherwise this thread will block and cause trouble
	// if the check is invoked from the admin page while there are active user connections.
	status = child_task("kiwi.update", fetch_makefile_ctask, POLL_MSEC(1000));

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		lprintf("UPDATE: failed to get latest version information from server\n");
        fail_reason = FAIL_MAKEFILE;
		if (report) report_result(conn);
		goto common_return;
	}

	{
		kstr_t *ver = read_file_string_reply("/root/web-888.latest");
		int n = sscanf(kstr_sp(ver), "%d.%d", &pending_maj, &pending_min);

		ver_changed = (n == 2 && (pending_maj > version_maj  || (pending_maj == version_maj && pending_min > version_min)));
		update_install = (admcfg_bool("update_install", NULL, CFG_REQUIRED) == true);
	}
	
	if (force_check) {
		if (ver_changed)
			lprintf("UPDATE: version changed (current %d.%d, new %d.%d), but check only\n",
				version_maj, version_min, pending_maj, pending_min);
		else
			lprintf("UPDATE: running most current version\n");
		
		report_result(conn);
		goto common_return;
	} else

	if (ver_changed && !update_install) {
		lprintf("UPDATE: version changed (current %d.%d, new %d.%d), but update install not enabled\n",
			version_maj, version_min, pending_maj, pending_min);
	} else
	
	if (ver_changed || force_build) {
		lprintf("UPDATE: version changed, current %d.%d, new %d.%d\n",
			version_maj, version_min, pending_maj, pending_min);
		lprintf("UPDATE: installing new version..\n");

		u4_t build_time = timer_sec();
		status = child_task("kiwi.build", update_build_ctask, POLL_MSEC(1000), TO_VOID_PARAM(force_build));
		
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            lprintf("UPDATE: Build failed, check /root/build.log file\n");
            fail_reason = FAIL_BUILD;
		    if (force_build) report_result(conn);
		    goto common_return;
		}
		
		lprintf("UPDATE: build took %d secs\n", timer_sec() - build_time);
		lprintf("UPDATE: switching to new version %d.%d\n", pending_maj, pending_min);
		if (admcfg_int("update_restart", NULL, CFG_REQUIRED) == 0) {
		    kiwi_exit(0);
		} else {
		    lprintf("UPDATE: rebooting Beagle..\n");
		    system("sleep 3; reboot");
		}
	} else {
		lprintf("UPDATE: version %d.%d is current\n", version_maj, version_min);
	}
	
	if (do_daily_restart) {
        if (kiwi.daily_restart == DAILY_RESTART) {
            lprintf("UPDATE: daily restart..\n");
            kiwi_exit(0);
        }

        if (kiwi.daily_restart == DAILY_REBOOT) {
            lprintf("UPDATE: daily reboot..\n");
            system("sleep 3; reboot");
        }
    }

common_return:
	if (file_auto_download_oneshot) {
	    file_auto_download_oneshot = false;
        //printf("file_GET: update check normal\n");
	    file_GET(TO_VOID_PARAM(FILE_DOWNLOAD_DIFF_RESTART));
	}

	if (conn) conn->update_check = WAIT_UNTIL_NO_USERS;     // restore default
	update_pending = update_task_running = update_in_progress = false;
}

// called at update check TOD, on each user logout in case update is pending or on demand by admin UI
void check_for_update(update_check_e type, conn_t *conn)
{
	bool force = (type != WAIT_UNTIL_NO_USERS);
	
	if (!force && admcfg_bool("update_check", NULL, CFG_REQUIRED) == false) {
		//printf("UPDATE: exiting because admin update check not enabled\n");
	
        if (file_auto_download_check) {
            file_auto_download_check = false;
            //printf("file_GET: update check false\n");
            file_GET(TO_VOID_PARAM(FILE_DOWNLOAD_DIFF_RESTART));
        }

		return;
	}
	
	if (force) {
		lprintf("UPDATE: force %s by admin\n", (type == FORCE_CHECK)? "update check" : "build");
		assert(conn != NULL);
		if (update_task_running) {
			lprintf("UPDATE: update task already running\n");
			report_result(conn);
			return;
		} else {
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
static int 	prev_update_window = -1;

// called at the top of each minute
void schedule_update(int min)
{
	#define UPDATE_SPREAD_HOURS	5	// # hours to spread updates over
	#define UPDATE_SPREAD_MIN	(UPDATE_SPREAD_HOURS * 60)

	#define UPDATE_START_HOUR	1	// 1 AM local time
	#define UPDATE_END_HOUR		(UPDATE_START_HOUR + UPDATE_SPREAD_HOURS)
	
	// relative to local time if timezone has been determined, utc otherwise
    int local_hour;
    (void) local_hour_min_sec(&local_hour);

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
            #define SERNO_MIN_TRIG1 1   // set to minute in the future test update should start
            #define SERNO_MIN_TRIG2 2   // set to minute in the future test update should start
		    serno = (mins_now <= SERNO_MIN_TRIG1)? SERNO_MIN_TRIG1 : SERNO_MIN_TRIG2;
		    int mins_trig = serno % UPDATE_SPREAD_MIN;
		    int hr_trig = UPDATE_START_HOUR + mins_trig/60;
		    int min_trig = mins_trig % 60;
            printf("TEST_UPDATE: %02d:%02d mins_now=%d mins_trig=%d (%02d:%02d sn=%d)\n",
                local_hour, min, mins_now, mins_trig, hr_trig, min_trig, serno);
        #endif

        update_window = update_window && (mins_now == (serno % UPDATE_SPREAD_MIN));

        if (prev_update_window == -1) prev_update_window = update_window? 1:0;
        first_update_window = ((prev_update_window == 0) && update_window);
		#ifdef TEST_UPDATE
            printf("TEST_UPDATE: update_window=%d prev_update_window=%d first_update_window=%d\n",
                update_window, prev_update_window, first_update_window);
        #endif
        prev_update_window = update_window? 1:0;

		if (update_window) {
		    printf("TLIMIT-IP 24hr cache cleared\n");
		    json_release(&cfg_ipl);
            json_init(&cfg_ipl, (char *) "{}", "cfg_ipl");      // clear 24hr ip address connect time limit cache
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
    
    do_daily_restart = first_update_window && !update_on_startup && (kiwi.daily_restart != DAILY_RESTART_NO);
    file_auto_download_check = first_update_window && !update_on_startup;

    //printf("min=%d file_auto_download_check=%d update_window=%d update_on_startup=%d\n",
    //    timer_sec()/60, file_auto_download_check, update_window, update_on_startup);
    
    if (update_on_startup && admcfg_int("restart_update", NULL, CFG_REQUIRED) != 0) {
		lprintf("UPDATE: update on restart delayed until update window\n");
		update_on_startup = false;
    }

	if (update_window || update_on_startup) {
		lprintf("UPDATE: check scheduled %s\n", update_on_startup? "(startup)":"");
		update_on_startup = false;
		update_pending = true;
		check_for_update(WAIT_UNTIL_NO_USERS, NULL);
	}
}
