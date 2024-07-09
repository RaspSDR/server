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

// Copyright (c) 2014-2022 John Seamons, ZL4VO/KF6VO

#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "valgrind.h"
#include "rx.h"
#include "clk.h"
#include "misc.h"
#include "str.h"
#include "web.h"
#include "peri.h"
#include "eeprom.h"
#include "gps_.h"
#include "coroutines.h"
#include "cfg.h"
#include "net.h"
#include "ext_int.h"
#include "sanitizer.h"
#include "shmem.h"      // shmem_init()
#include "debug.h"

#ifdef EV_MEAS
    #warning NB: EV_MEAS is enabled
#endif

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <time.h>
#include <sys/resource.h>
#include <sched.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>
#include <fftw3.h>

kiwi_t kiwi;

int version_maj, version_min;
int rx_chans, wf_chans, nrx_samps, snd_rate, rx_decim;

int ev_dump=0, tone, down, gps_chans=GPS_MAX_CHANS, rx_num, wf_num,
	navg=1, meas, monitors_max, bg,
	print_stats, debian_maj, debian_min, test_flag, dx_print,
	use_foptim, is_locked, drm_nreg_chans;

u4_t ov_mask;

bool create_eeprom, need_hardware, kiwi_reg_debug, have_ant_switch_ext,
    disable_led_task, debug_printfs, cmd_debug;

int main_argc;
char **main_argv;
static bool _kiwi_restart;

void kiwi_restart()
{
	// leak detector needs exit while running on main() stack
	_kiwi_restart = true;
	TaskWakeupF(TID_MAIN, TWF_CANCEL_DEADLINE);
	while (1)
		TaskSleepSec(1);
}

int main(int argc, char *argv[])
{
	bool err;
	
	version_maj = VERSION_MAJ;
	version_min = VERSION_MIN;
	
	main_argc = argc;
	main_argv = argv;
	
	// enable generation of core file in /tmp
	//scall("core_pattern", system("echo /tmp/core-%e-%s-%p-%t > /proc/sys/kernel/core_pattern"));
	
	// use same filename to prevent looping dumps from filling up filesystem
	scall("core_pattern", system("echo /tmp/core-%e > /proc/sys/kernel/core_pattern"));
	const struct rlimit unlim = { RLIM_INFINITY, RLIM_INFINITY };
	scall("setrlimit", setrlimit(RLIMIT_CORE, &unlim));
	system("rm -f /tmp/core-*");     // remove old core files
	
	kiwi.platform = PLATFORM_ZYNQ7010;

	fftwf_make_planner_thread_safe();
	kstr_init();
	shmem_init();
	printf_init();
	
	system("mkdir -p " DIR_DATA);

    #define ARG(s) (strcmp(argv[ai], s) == 0)
    #define ARGP() argv[++ai]
    #define ARGL(v) if (ai+1 < argc) (v) = strtol(argv[++ai], 0, 0);
    
	for (int ai = 1; ai < argc; ) {
	    if (strncmp(argv[ai], "-o_", 3) == 0) {
	        ai++;
		    while (ai < argc && ((argv[ai][0] != '+') && (argv[ai][0] != '-'))) ai++;
	        continue;
	    }
	
		if (ARG("-kiwi_reg")) kiwi_reg_debug = TRUE; else
		if (ARG("-cmd_debug")) cmd_debug = TRUE; else
		if (ARG("-bg")) { background_mode = TRUE; bg=1; } else
		if (ARG("-log")) { log_foreground_mode = TRUE; } else
		if (ARG("-fopt")) use_foptim = 1; else   // in EDATA_DEVEL mode use foptim version of files
		if (ARG("-down")) down = 1; else
		if (ARG("-debug")) debug_printfs = true; else
		if (ARG("-stats") || ARG("+stats")) { print_stats = STATS_TASK; ARGL(print_stats); } else
		if (ARG("-v")) {} else      // dummy arg so Kiwi version can appear in e.g. htop
		
		if (ARG("-test")) { ARGL(test_flag); printf("test_flag %d(0x%x)\n", test_flag, test_flag); } else
		if (ARG("-dx")) { ARGL(dx_print); printf("dx %d(0x%x)\n", dx_print, dx_print); } else
		if (ARG("-led") || ARG("-leds")) disable_led_task = true; else

		if (ARG("-debian")) {} else     // dummy arg so Kiwi version can appear in e.g. htop
		if (ARG("-ctrace")) { ARGL(web_caching_debug); } else
		if (ARG("-ext")) kiwi.ext_clk = true; else
		if (ARG("-eeprom")) create_eeprom = true; else
		if (ARG("-dump") || ARG("+dump")) { ARGL(ev_dump); } else
		if (ARG("-meas")) meas = 1; else
		
		if (ARG("-avg")) { ARGL(navg); } else
		if (ARG("-tone")) { ARGL(tone); } else
		if (ARG("-rx")) { ARGL(rx_num); } else
		if (ARG("-wf")) { ARGL(wf_num); } else
		if (ARG("-ch")) { ARGL(gps_chans); } else
		
		lprintf("unknown arg: \"%s\"\n", argv[ai]);

		ai++;
		while (ai < argc && ((argv[ai][0] != '+') && (argv[ai][0] != '-'))) ai++;
	}
	
	lprintf("KiwiSDR v%d.%d --------------------------------------------------------------------\n",
		version_maj, version_min);
    lprintf("compiled: %s %s on %s\n", __DATE__, __TIME__, COMPILE_HOST);

    char *reply = read_file_string_reply("/etc/alpine-release");
    if (reply == NULL) panic("debian_version");
    if (sscanf(kstr_sp(reply), "%d.%d", &debian_maj, &debian_min) != 2) panic("debian_version");
    kstr_free(reply);
    lprintf("/etc/debian_version %d.%d\n", debian_maj, debian_min);
    
    #if defined(USE_ASAN)
    	lprintf("### compiled with USE_ASAN\n");
    #endif
    
    #if defined(HOST) && defined(USE_VALGRIND)
    	lprintf("### compiled with USE_VALGRIND\n");
    #endif
    
    assert(TRUE != FALSE);
    assert(true != false);
    assert(TRUE == true);
    assert(FALSE == false);
    assert(NOT_FOUND != TRUE);
    assert(NOT_FOUND != FALSE);
    
    debug_printfs |= kiwi_file_exists(DIR_CFG "/opt.debug")? 1:0;

	TaskInit();
	misc_init();
    cfg_reload();

    bool update_admcfg = false;
    kiwi.anti_aliased = admcfg_default_bool("anti_aliased", false, &update_admcfg);
	kiwi.airband = admcfg_default_bool("airband", false, &update_admcfg);
	kiwi.wf_share = admcfg_default_bool("wf_share", false, &update_admcfg);

    if (update_admcfg) admcfg_save_json(cfg_adm.json);      // during init doesn't conflict with admin cfg

    clock_init();
	peri_init();

	uint32_t signature = fpga_signature();
    rx_chans = signature & 0x0f;
	if (kiwi.wf_share)
		wf_chans = rx_chans; // always give most wf channels
	else
		wf_chans = (signature >> 8) & 0x0f;

    snd_rate = SND_RATE_4CH;
    rx_decim = (int)(ADC_CLOCK_TYP/12000); // 12k

    bool no_wf = cfg_bool("no_wf", &err, CFG_OPTIONAL);
    if (err) no_wf = false;
    if (no_wf) wf_chans = 0;

    lprintf("firmware: rx_chans=%d wf_chans=%d gps_chans=%d\n", rx_chans, wf_chans, gps_chans);

    assert(rx_chans <= MAX_RX_CHANS);
    assert(wf_chans <= MAX_WF_CHANS);

    nrx_samps = NRX_SAMPS_CHANS(rx_chans);
    lprintf("firmware: RX rx_decim=%d USE_RX_CICF=%d\n", rx_decim, 0);
    lprintf("firmware: RX srate=%.3f(%d) samps=%d\n",
        ext_update_get_sample_rateHz(ADC_CLK_TYP), snd_rate, nrx_samps);

    assert(nrx_samps <= MAX_NRX_SAMPS);
    assert(nrx_samps < FASTFIR_OUTBUF_SIZE);    // see data_pump.h

    rx_num = rx_chans, wf_num = wf_chans;
    monitors_max = (rx_chans * N_CAMP) + N_QUEUERS;
    
	TaskInitCfg();
   
	need_hardware = true;

	// called early, in case another server already running so we can detect the busy socket and bail
	web_server_init(WS_INIT_CREATE);

	if (need_hardware) {
		kiwi.ext_clk = cfg_bool("ext_ADC_clk", &err, CFG_OPTIONAL);
		if (err) kiwi.ext_clk = false;
		
		net.dna = fpga_dna();
		printf("device DNA %08x|%08x\n", PRINTF_U64_ARG(net.dna));
	}
	
	rx_server_init();

	extint_setup();

	web_server_init(WS_INIT_START);
    
    // switch to ext clock only after GPS clock switch occurs
    if (kiwi.ext_clk) {
        printf("switching to external ADC clock..\n");
        kiwi_msleep(100);
    }
	
	gps_main(argc, argv);
    
	CreateTask(stat_task, NULL, MAIN_PRIORITY);
    
	// run periodic housekeeping functions
	while (TRUE) {
	
		TaskCollect();
		TaskCheckStacks(false);

		TaskSleepReasonSec("main loop", 10);
		
        if (_kiwi_restart) kiwi_exit(0);
	}
}
