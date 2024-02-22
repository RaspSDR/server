#include "gps.h"
//#include "gps/GNSS-SDRLIB/rtklib.h"
// TODO: Use libgps to simulate the necessary APIs
gps_t gps;

const char *Week[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

void gps_main(int argc, char *argv[])
{
    memset(&gps, sizeof(gps), 0);

    // create a task to pull gps
}

void ChanRemove(sat_e type)
{
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
