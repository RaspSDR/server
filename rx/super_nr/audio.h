#pragma once

#include "super_nr.h"

class SuperNR {
public:
    SuperNR() : state(), samples() {}
    ~SuperNR() { super_nr_destroy(&state); }
    SuperNR(const SuperNR&) = delete;
    SuperNR& operator=(const SuperNR&) = delete;

    super_nr_status init(float sample_rate);
    super_nr_status reset();
    void destroy();
    super_nr_status process(int count, int16_t *pcm, int bandwidth, int level,
                            float output_gain = 1.0f);

private:
    super_nr state;
    float samples[SUPER_NR_HOP];
};
