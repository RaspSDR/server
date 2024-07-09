#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "coroutines.h"
#include "peri.h"
#include "gps_.h"
#include "clk.h"

#include <gps.h>
#include <math.h>
#include <unistd.h>

gps_t gps;
lock_t gps_lock;
SATELLITE Sats[MAX_SATS];

extern int Sats_num;

const char *Week[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static struct gps_data_t gps_handle;

static void gps_task(void *param);

void gps_main(int argc, char *argv[])
{
    gps.start = timer_ms();
    gps.last_samp_hour = -1;
    gps.last_samp = -1;

    memset(Sats, 0xff, sizeof(Sats));
    lock_init(&gps_lock);

    if (0 != gps_open("localhost", "2947", &gps_handle))
    {
        printf("open gpsd failed\n");
        return;
    }

#if 0
    // enable satellite for gps
    // Construct the command
    // Content is coming from gpsctl -D 4 command
    const char *command = "?DEVICE={\"path\":\"/dev/ttyPS1\",\"hexdata\":\"245043415330332c312c312c312c312c312c312c312c312c302c302c2c2c312c312c2c2c2c312a33330d0a\"}\n";
    
    // Send the command to GPSD
    if (write(gps_handle.gps_fd, command, strlen(command))) {
        fprintf(stderr, "Enable satellite on gps failed\n");
    }
#endif

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

static int find_sat(sat_e type, int prn)
{
    int i;
    for (i = 0; i < MAX_SATS && Sats[i].prn != -1; i++)
    {
        if (Sats[i].prn == prn && Sats[i].type == type)
            return i;
    }

    Sats[i].prn = prn;
    Sats[i].type = type;
    snprintf(Sats[i].prn_s, sizeof(Sats[i].prn_s), "%c%d", sat_s[type], prn);

    return i;
}

// LLH -> XYZ  (LLH = LonLatAlt)
static void LLH2XYZ(double lat, double lon, double alt, double *x, double *y)
{
    double const cp = std::cos(lon), sp = std::sin(lon);
    double const cl = std::cos(lat), sl = std::sin(lat);
    double const nu = rc_normal(alt);
    *x = (nu + alt) * cp * cl;
    *y = (nu + alt) * sp * sl;
    // z = ((1-e2())*nu + llh.alt())*sp}});
}

static void update_gps_info_before(int samp_hour, int samp_min)
{
    if (gps.last_samp_hour != samp_hour) {
        gps.fixes_hour = gps.fixes_hour_incr;
        gps.fixes_hour_incr = 0;
        gps.fixes_hour_samples++;
        gps.last_samp_hour = samp_hour;
    }
        
    //printf("GPS last_samp=%d samp_min=%d fixes_min=%d\n", gps.last_samp, samp_min, gps.fixes_min);
    if (gps.last_samp != samp_min) {
        //printf("GPS fixes_min=%d fixes_min_incr=%d\n", gps.fixes_min, gps.fixes_min_incr);
        gps.fixes_min = gps.fixes_min_incr;
        gps.fixes_min_incr = 0;
        for (int sat = 0; sat < MAX_SATS; sat++) {
            gps.az[gps.last_samp][sat] = 0;
            gps.el[gps.last_samp][sat] = 0;
        }
        gps.last_samp = samp_min;
    }
}

static void gps_task(void *param)
{
    double t_rx = 0;
    fpga_start_pps();
    for (;;)
    {
        // fetch pps data
        u64_t ticks;
        ticks = fpga_read_pps(); 

        if (ticks)
            clock_correction(t_rx, ticks);

        if (!gps_waiting(&gps_handle, 1000000))
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

        lock_enter(&gps_lock);
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

                update_gps_info_before(((int)d/60/60) % 24, ((int)d/60) % 60);

                t_rx = d;
            }

            gps.tLS_valid = true;
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
            memset(gps.shadow_map, 0, sizeof(gps.shadow_map));
            memset(gps.ch, 0, sizeof(gps.ch));
            int i;
            for (i = 0; i < gps_handle.satellites_visible && i < GPS_MAX_CHANS; i++)
            {
                int sat = find_sat((sat_e)gps_handle.skyview[i].gnssid, gps_handle.skyview[i].PRN);
                gps.ch[i].snr = gps_handle.skyview[i].ss;
                gps.ch[i].sat = sat;
                gps.ch[i].el = gps_handle.skyview[i].elevation;
                gps.ch[i].az = gps_handle.skyview[i].azimuth;

                gps.ch[i].has_soln = gps_handle.skyview[i].used;

                // already have az/el for this sat in this sample period? 
                if (gps.el[gps.last_samp][sat])
                    continue;

                const int el = std::round(gps.ch[i].el);
                const int az = std::round(gps.ch[i].az);

                // printf("%s NEW EL/AZ=%2d %3d\n", PRN(sat), el, az);
                if (az < 0 || az >= 360 || el <= 0 || el > 90)
                    continue;

                gps.shadow_map[az] |= (1 << int(std::round(el / 90.0 * 31.0)));

                // special treatment for QZS_3
                SATELLITE *s = &Sats[sat];
                if (s->type == QZSS && s->prn == 199)
                {
                    gps.qzs_3.az = az;
                    gps.qzs_3.el = el;
                }

                gps.el[gps.last_samp][sat] = el;
                gps.az[gps.last_samp][sat] = az;
            }
            for(; i < GPS_MAX_CHANS; i++)
            {
                gps.ch[i].sat = -1;
            }
        }

        gps.fixes++; gps.fixes_min_incr++; gps.fixes_hour_incr++;
        
        // at startup immediately indicate first solution
        if (gps.fixes_min == 0) gps.fixes_min++;

        // at startup incrementally update until first hour sample period has ended
        if (gps.fixes_hour_samples <= 1) gps.fixes_hour++;

        lock_leave(&gps_lock);
    }
}