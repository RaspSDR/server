#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "coroutines.h"
#include "fpga.h"
#include "gps_.h"
#include "clk.h"

#include <gps.h>
#include <math.h>

gps_t gps;

const char *Week[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
#define MODE_STR_NUM 4
static const char *mode_str[MODE_STR_NUM] = {
    "n/a",
    "None",
    "2D",
    "3D"};

static struct gps_data_t gps_handle;

static void gps_task(void *param);

void gps_main(int argc, char *argv[])
{
    memset(&gps, sizeof(gps), 0);
    gps.start = timer_ms();

    if (0 != gps_open("localhost", "2947", &gps_handle))
    {
        printf("open gpsd failed\n");
        return;
    }

    // Set non-blocking mode
    if (gps_stream(&gps_handle, WATCH_ENABLE | WATCH_JSON, NULL) != 0)
    {
        printf("Error: Unable to enable GPS streaming\n");
        return;
    }

    // create a task to pull gps
    CreateTaskF(gps_task, 0, GPS_PRIORITY, CTF_NO_LOG);
}

/* convert calendar day/time to time -------------------------------------------
 * convert calendar day/time to gtime_t struct
 * args   : double *ep       I   day/time {year,month,day,hour,min,sec}
 * return : gtime_t struct
 * notes  : proper in 1970-2037 or 1970-2099 (64bit time_t)
 *-----------------------------------------------------------------------------*/
gtime_t epoch2time(const double *ep)
{
    const int doy[] = {1, 32, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335};
    gtime_t time = {0};
    int days, sec, year = (int)ep[0], mon = (int)ep[1], day = (int)ep[2];

    if (year < 1970 || 2099 < year || mon < 1 || 12 < mon)
        return time;

    /* leap year if year%4==0 in 1901-2099 */
    days = (year - 1970) * 365 + (year - 1969) / 4 + doy[mon - 1] + day - 2 + (year % 4 == 0 && mon >= 3 ? 1 : 0);
    sec = (int)floor(ep[5]);
    time.time = (time_t)days * 86400 + (int)ep[3] * 3600 + (int)ep[4] * 60 + sec;
    time.sec = ep[5] - sec;
    return time;
}

const static double gpst0[] = {1980, 1, 6, 0, 0, 0}; /* gps time reference */

gtime_t gpst2time(int week, double sec)
{
    gtime_t t = epoch2time(gpst0);

    if (sec < -1e9 || 1e9 < sec)
        sec = 0.0;
    t.time += 86400 * 7 * week + (int)sec;
    t.sec = sec - (int)sec;
    return t;
}

static const double _a = 6378137.0;
static const double _b = 6356752.31424518;
static const double a2 = _a*_a;
static const double b2 = _b*_b;
static const double e2 =  1.0 - b2/a2;
static const double a = _a;

static double rc_normal(double phi) {
    double const sp = std::sin(phi);
    return a/std::sqrt(1.0 - e2*sp*sp);
}

// LLH -> XYZ  (LLH = LonLatAlt)
static void LLH2XYZ(double lat, double lon, double alt, double *x, double *y)
{
    double const cp = std::cos(lon), sp = std::sin(lon);
    double const cl = std::cos(lat), sl = std::sin(lat);
    double const nu = rc_normal(alt);
    *x = (nu + alt) * cp * cl;
    *y = (nu + alt) * cp * sl;
    // z = ((1-e2())*nu + llh.alt())*sp}});
}

static void gps_task(void *param)
{
    fpga_config->reset |= RESET_PPS;
    for (;;)
    {
        TaskSleepMsec(500);

        // fetch pps data
        while (fpga_status->pps_fifo > 0)
        {
            u64_t ticks = *fpga_pps_data;
            clock_correction(ticks);
        }

        if (!gps_waiting(&gps_handle, 0))
            continue;

        gps_read(&gps_handle, NULL, 0);

        // print_gps_data(&gps_handle);

        if (MODE_SET != (MODE_SET & gps_handle.set))
        {
            // did not even get mode, nothing to see here
            gps.acquiring = false;
            continue;
        }
        else
        {
            gps.acquiring = true;
        }

        // Check if the receiver has obtained a position fix
        if ((gps_handle.fix.mode > MODE_NO_FIX) && (gps.ttff == 0)) {
            gps.ttff = (timer_ms() - gps.start) / 1000;
        }

        if (TIME_SET == (TIME_SET & gps_handle.set))
        {
            double d = gps_handle.fix.time.tv_sec;
            if (d != 0)
            {
                gps.StatDay = d / (60 * 60 * 24);
                if (gps.StatDay < 0 || gps.StatDay >= 7)
                {
                    gps.StatDay = -1;
                }
                gps.StatDaySec = d - (60 * 60 * 24) * gps.StatDay;
                gps.StatWeekSec = d;
            }

            gps.delta_tLS = gps_handle.leap_seconds;
        }

        if (LATLON_SET == (LATLON_SET & gps_handle.set))
        {
            gps.sgnLat = gps_handle.fix.latitude;
            gps.StatLat = fabs(gps_handle.fix.latitude);
            gps.StatNS = gps_handle.fix.latitude < 0 ? 'S' : 'N';

            gps.sgnLon = gps_handle.fix.longitude;
            gps.StatLon = fabs(gps_handle.fix.longitude);
            gps.StatEW = gps_handle.fix.longitude < 0 ? 'W' : 'E';
        }

        if (ALTITUDE_SET == (ALTITUDE_SET & gps_handle.set))
        {
            gps.StatAlt = gps_handle.fix.altitude;
        }

        if (gps.sgnLon != 0)
        {
            double x, y;
            LLH2XYZ(gps.sgnLat, gps.sgnLon, gps.StatAlt, &x, &y);

            gps_pos_t *pos = &gps.POS_data[gps.POS_seq_w];

            pos->lat = gps.sgnLat;
            pos->lon = gps.sgnLon;
            pos->x = x;
            pos->y = y;

            gps.POS_seq_w = (gps.POS_seq_w + 1) % GPS_POS_SAMPS;
            if (gps.POS_len < GPS_POS_SAMPS) gps.POS_len++;
        }

        if (gps_handle.set & SATELLITE_SET)
        {
            gps.FFTch = gps_handle.satellites_visible;
            gps.tracking = gps_handle.satellites_visible;
            gps.good = gps_handle.satellites_visible;
            gps.fixes = gps_handle.satellites_used;
            for (int i = 0; i < gps_handle.satellites_visible && i < GPS_MAX_CHANS; i++)
            {
                gps.ch[i].snr = gps_handle.skyview[i].ss;
                gps.ch[i].sat = gps_handle.skyview[i].PRN;
                gps.ch[i].el = gps_handle.skyview[i].elevation;
                gps.ch[i].az = gps_handle.skyview[i].azimuth;
                gps.ch[i].type = sat_s[gps_handle.skyview[i].gnssid];
            }
        }
    }
}