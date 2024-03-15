//////////////////////////////////////////////////////////////////////////
// Homemade GPS Receiver
// Copyright (C) 2013 Andrew Holme
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// http://www.holmea.demon.co.uk/GPS/Main.htm
//////////////////////////////////////////////////////////////////////////

#pragma once

#include "types.h"
#include "kiwi.h"
#include "config.h"
#include "printf.h"
#include "coroutines.h"

#include <inttypes.h>
#include <math.h>

// select debugging
//#define TEST_VECTOR
#define	QUIET

void gps_main(int argc, char *argv[]);


///////////////////////////////////////////////////////////////////////////////
// Official GPS constants

const double PI = 3.1415926535898;

const double MU = 3.986005e14;          // WGS 84: earth's gravitational constant for GPS user
const double OMEGA_E = 7.2921151467e-5; // WGS 84: earth's rotation rate

const double C = 2.99792458e8; // Speed of light

const double F = -4.442807633e-10; // -2*sqrt(MU)/pow(C,2)

///////////////////////////////////////////////////////////////////////////////

typedef enum { Navstar, SBAS, E1B, BeiDou, IMESS, QZSS, GLONASS } sat_e;
const char sat_s[] = { 'N', 'S', 'E', 'B', 'I', 'Q', 'G' };

#define MAX_SATS    64


//////////////////////////////////////////////////////////////
// User interface

typedef enum {
    STAT_SAT,
    STAT_POWER,
    STAT_WDOG,
    STAT_SUB,
    STAT_LAT,
    STAT_LON,
    STAT_ALT,
    STAT_TIME,
    STAT_DOP,
    STAT_LO,
    STAT_CA,
    STAT_PARAMS,
    STAT_ACQUIRE,
    STAT_EPL,
    STAT_NOVFL,
    STAT_DEBUG,
    STAT_SOLN
} STAT;

#define GPS_ERR_SLIP    1
#define GPS_ERR_CRC     2
#define GPS_ERR_ALERT   3
#define GPS_ERR_OOS     4
#define GPS_ERR_PAGE    5

typedef struct {
    int az, el;
} azel_t;

typedef struct {
    int sat;
    int snr;
    int rssi, gain;
    char type;
    #define GPS_N_AGE (8 + SPACE_FOR_NULL)
    char age[GPS_N_AGE];
    bool too_old;
    int wdog;
    int hold, ca_unlocked, parity, alert;
    int sub, sub_renew;
    int novfl, frames, par_errs;
    int az, el;
    int has_soln;
    int ACF_mode;
} gps_chan_t;

typedef struct {
    int x, y;
    float lat, lon;
} gps_pos_t;

typedef struct {
    float lat, lon;
} gps_map_t;

typedef struct {
    int n_Navstar, n_QZSS, n_E1B;
	bool acquiring, tLS_valid;
	unsigned start, ttff;
	int tracking, good, FFTch;

    int last_samp_hour;
	u4_t fixes, fixes_min, fixes_min_incr;
	u4_t fixes_hour, fixes_hour_incr, fixes_hour_samples;

	double StatWeekSec, StatDaySec;
	int StatDay;    // 0 = Sunday
	double StatLat, StatLon, StatAlt, sgnLat, sgnLon;
	char StatNS, StatEW;
    signed delta_tLS, delta_tLSF;
    bool include_alert_gps;
    bool include_E1B;
    bool set_date, date_set;
    int tod_chan;
    int soln_type;
	gps_chan_t ch[GPS_MAX_CHANS];
	
	//#define AZEL_NSAMP (4*60)
	#define AZEL_NSAMP 60
	int az[AZEL_NSAMP][MAX_SATS];
	int el[AZEL_NSAMP][MAX_SATS];
	int last_samp;
	
	u4_t shadow_map[360];
	azel_t qzs_3;

    #define GPS_POS_SAMPS 64
	gps_pos_t POS_data[GPS_POS_SAMPS];
	u4_t POS_seq, POS_len, POS_seq_w, POS_seq_r;

    #define GPS_MAP_SAMPS 16
	gps_map_t MAP_data_seq[GPS_MAP_SAMPS];
	u4_t MAP_next, MAP_len, MAP_seq_w, MAP_seq_r;

} gps_t;

extern gps_t gps;

extern const char *Week[];

struct UMS {
    int u, m;
    double fm, s;
    UMS(double x) {
        u = trunc(x); x = (x-u)*60; fm = x;
        m = trunc(x); s = (x-m)*60;
    }
};

typedef struct {        /* time struct */
    time_t time;        /* time (s) expressed by standard time_t */
    double sec;         /* fraction of second under 1 s */
} gtime_t;