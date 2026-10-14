#include "FST4WReporter.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static bool valid_grid(const char *grid)
{
    size_t n = strlen(grid);
    return (n == 4 || n == 6) &&
        grid[0] >= 'A' && grid[0] <= 'R' && grid[1] >= 'A' && grid[1] <= 'R' &&
        grid[2] >= '0' && grid[2] <= '9' && grid[3] >= '0' && grid[3] <= '9' &&
        (n == 4 || (grid[4] >= 'A' && grid[4] <= 'X' && grid[5] >= 'A' && grid[5] <= 'X'));
}

bool fst4w_report_url(const char *encoded_rcall, const char *rgrid,
    double dial_freq_hz, int tr_period, const struct tm& slot,
    float audio_freq_hz, float snr, float dt, const char *message,
    std::string& url)
{
    url.clear();
    int mode;
    switch (tr_period) {
        case 120: mode = 3; break;
        case 300: mode = 5; break;
        case 900: mode = 15; break;
        case 1800: mode = 30; break;
        default: return false;
    }

    char receiver_grid[7];
    if (!encoded_rcall || !*encoded_rcall || strlen(encoded_rcall) > 64 ||
        !rgrid || strlen(rgrid) >= sizeof(receiver_grid))
        return false;
    strcpy(receiver_grid, rgrid);
    for (char *p = receiver_grid; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
    if (!valid_grid(receiver_grid))
        return false;

    char call[16], grid[8], power[4], extra[2];
    int fields = sscanf(message, "%15s %7s %3s %1s", call, grid, power, extra);
    if (fields < 2 || fields > 3)
        return false;

    size_t n = strlen(call);
    if (call[0] == '<' && n > 2 && call[n - 1] == '>') {
        call[n - 1] = '\0';
        memmove(call, call + 1, n - 1);
    }
    bool have_letter = false, have_digit = false;
    if (strlen(call) < 3 || strlen(call) > 13)
        return false;
    for (const char *p = call; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') have_letter = true;
        else if (*p >= '0' && *p <= '9') have_digit = true;
        else if (*p != '/') return false;
    }
    if (!have_letter || !have_digit)
        return false;

    for (char *p = grid; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
    if (valid_grid(grid)) {
        if (fields == 2) power[0] = '\0';
    } else if (fields == 2) {
        if (strlen(grid) >= sizeof(power)) return false;
        strcpy(power, grid);
        grid[0] = '\0';
    } else {
        return false;
    }
    if (power[0]) {
        char *end;
        long dbm = strtol(power, &end, 10);
        if (*end || dbm < 0 || dbm > 60)
            return false;
    }

    char request[512];
    // WSPRnet's receiver frequency is the passband center, not the USB dial.
    int len = snprintf(request, sizeof(request),
        "http://wsprnet.org/post/?function=wspr&rcall=%s&rgrid=%s&rqrg=%.6f&"
        "date=%02d%02d%02d&time=%02d%02d&sig=%.0f&dt=%.1f&drift=0&"
        "tqrg=%.6f&tcall=%s&tgrid=%s&dbm=%s&version=web-888&mode=%d",
        encoded_rcall, receiver_grid, (dial_freq_hz + 1500) / 1e6,
        (slot.tm_year + 1900) % 100, slot.tm_mon + 1, slot.tm_mday,
        slot.tm_hour, slot.tm_min, roundf(snr), dt,
        (dial_freq_hz + audio_freq_hz) / 1e6, call, grid, power, mode);
    if (len < 0 || (size_t) len >= sizeof(request))
        return false;
    url.assign(request, len);
    return true;
}

static size_t report_reply(void *data, size_t size, size_t count, void *param)
{
    std::string *reply = static_cast<std::string *>(param);
    if (size && count > 4096 / size)
        return 0;
    size_t len = size * count;
    if (reply->size() + len > 4096)
        return 0;
    reply->append(static_cast<const char *>(data), len);
    return len;
}

static bool report_added(const std::string& reply)
{
    size_t pos = reply.find("spot(s) added");
    if (pos == std::string::npos)
        return false;
    size_t out_of = reply.find(" out of");
    if (out_of != std::string::npos && out_of < pos)
        pos = out_of;
    while (pos && reply[pos - 1] == ' ') pos--;
    size_t end = pos;
    while (pos && reply[pos - 1] >= '0' && reply[pos - 1] <= '9') pos--;
    return pos < end && strtol(reply.c_str() + pos, NULL, 10) > 0;
}

void fst4w_report_upload(void *param)
{
    const fst4w_upload_t *upload = static_cast<const fst4w_upload_t *>(param);
    for (const std::string& url : upload->urls) {
        CURL *curl = curl_easy_init();
        if (!curl) {
            fprintf(stderr, "FST4W WSPRnet RX%d: unable to initialize upload\n", upload->rx_chan);
            return;
        }
        std::string reply;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "web-888");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, report_reply);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply);
        CURLcode status = curl_easy_perform(curl);
        if (status != CURLE_OK) {
            fprintf(stderr, "FST4W WSPRnet RX%d upload failed: %s\n",
                upload->rx_chan, curl_easy_strerror(status));
        } else if (!report_added(reply)) {
            fprintf(stderr, "FST4W WSPRnet RX%d upload rejected: %s\n",
                upload->rx_chan, reply.c_str());
        } else if (upload->spot_log) {
            fprintf(stderr, "FST4W WSPRnet RX%d: %s\n", upload->rx_chan, reply.c_str());
        }
        curl_easy_cleanup(curl);
    }
}
