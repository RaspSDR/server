#pragma once

#include <string>
#include <vector>
#include <time.h>

struct fst4w_upload_t {
    int rx_chan;
    bool spot_log;
    std::vector<std::string> urls;
};

bool fst4w_report_url(const char *encoded_rcall, const char *rgrid,
    double dial_freq_hz, int tr_period, const struct tm& slot,
    float audio_freq_hz, float snr, float dt, const char *message,
    std::string& url);
void fst4w_report_upload(void *param);
