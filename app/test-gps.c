#include <gps.h>

static struct gps_data_t gps_handle;
const char sat_s[] = { 'N', 'S', 'E', 'B', 'I', 'Q', 'G' };

void print_gps_data(const struct gps_data_t *data) {
    if (data->set & MODE_SET) {
        printf("GPSD running\n");
    }

    if (data->set & LATLON_SET) {
        printf("Latitude: %f, Longitude: %f\n", data->fix.latitude, data->fix.longitude);
    }

    if (data->set & ALTITUDE_SET) {
        printf("Altitude: %f meters\n", data->fix.altitude);
    }

    if (data->set & SPEED_SET) {
        printf("Speed: %f meters/second\n", data->fix.speed);
    }

    if (data->set & TRACK_SET) {
        printf("Track: %f degrees\n", data->fix.track);
    }

    if (data->set & CLIMB_SET) {
        printf("Climb: %f meters/second\n", data->fix.climb);
    }

    if (data->set & TIME_SET) {
        printf("time: %llds\n", gps_handle.fix.time.tv_sec);
    }

    // Skyview data
    if (data->satellites_used > 0) {
        printf("Satellites Used: %d\n", data->satellites_used);
    }

    if (data->satellites_visible > 0) {
        printf("Satellites Visible: %d\n", data->satellites_visible);
        for (int i = 0; i < data->satellites_visible; i++) {
            printf("Satellite %d: PRN:%c%d, Elevation: %lf, Azimuth: %lf, SNR: %lf, Used: %s\n",
                   i + 1,
                   sat_s[data->skyview[i].gnssid],
                   data->skyview[i].PRN,
                   data->skyview[i].elevation,
                   data->skyview[i].azimuth,
                   data->skyview[i].ss,
                   data->skyview[i].used ? "yes" : "no");
        }
    }
}


int main()
{
    if (0 != gps_open("localhost", "2947", &gps_handle)) {
        printf("open gpsd failed\n");
        return 0;
    }

    // Set non-blocking mode
    if (gps_stream(&gps_handle, WATCH_ENABLE | WATCH_JSON, NULL) != 0) {
        printf("Error: Unable to enable GPS streaming\n");
        return 0;
    }

    for(int i = 0; ;i++) {
        if (!gps_waiting(&gps_handle, 0))
            continue;

        gps_read(&gps_handle, NULL, 0);

        print_gps_data(&gps_handle);

        usleep(2000);
    }
}