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
SATELLITE Sats[MAX_SATS];

const char *Week[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
#define MODE_STR_NUM 4
static const char *mode_str[MODE_STR_NUM] = {
    "n/a",
    "None",
    "2D",
    "3D"
};

static struct gps_data_t gps_handle;

static void gps_task(void *param);

void gps_main(int argc, char *argv[])
{
    memset(&gps, sizeof(gps), 0);

    if (0 != gps_open("localhost", "2947", &gps_handle)) {
        printf("open gpsd failed\n");
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

static void gps_task(void *param)
{
    fpga_config->reset |= RESET_PPS;
    for(;;)
    {
        /*
        if (-1 == gps_read(&gps_handle, NULL, 0)) {
            printf("Read error.  Bye, bye\n");
            break;
        }
        if (MODE_SET != (MODE_SET & gps_handle.set)) {
            // did not even get mode, nothing to see here
            continue;
        }
        if (0 > gps_handle.fix.mode ||
            MODE_STR_NUM <= gps_handle.fix.mode) {
            gps_handle.fix.mode = 0;
        }
        printf("Fix mode: %s (%d) Time: ",
               mode_str[gps_handle.fix.mode],
               gps_handle.fix.mode);
        if (TIME_SET == (TIME_SET & gps_handle.set)) {
            // not 32 bit safe
            printf("%ld.%09ld ", gps_handle.fix.time.tv_sec,
                   gps_handle.fix.time.tv_nsec);
        } else {
            puts("n/a ");
        }
        if (isfinite(gps_handle.fix.latitude) &&
            isfinite(gps_handle.fix.longitude)) {
            // Display data from the GPS receiver if valid.
            printf("Lat %.6f Lon %.6f\n",
                   gps_handle.fix.latitude, gps_handle.fix.longitude);
        } else {
            printf("Lat n/a Lon n/a\n");
        }
        */

        while (fpga_status->pps_fifo > 0)
        {
            u64_t ticks = *fpga_pps_data;
            clock_correction(ticks);
        }

        TaskSleepMsec(100);
    }
}