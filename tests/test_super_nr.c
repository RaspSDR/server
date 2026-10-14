#include "super_nr.h"

#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static super_nr state, other, saved;
static float samples[512], input[512], previous[512], packed[1024], real[1024];

static int failed_fft(void *context, const float *in, float *out, int inverse)
{
    (void)context;
    (void)in;
    (void)out;
    (void)inverse;
    return -1;
}

static void signal(unsigned frame)
{
    unsigned i;
    for (i = 0; i < 512; ++i) {
        input[i] = 100.0f * sinf((float)(frame * 512 + i) * 0.2f) +
                   20.0f * cosf((float)(frame * 512 + i) * 0.713f);
    }
    memcpy(samples, input, sizeof(input));
}

int main(void)
{
    unsigned i, level, frame;
    REQUIRE(super_nr_init(NULL, NULL, NULL) == SUPER_NR_INVALID_ARGUMENT);
    REQUIRE(super_nr_init(&state, NULL, NULL) == SUPER_NR_OK);
    for (i = 0; i < 512; ++i) {
        float a = state.spectral.window[i], b = state.spectral.window[i + 512];
        REQUIRE(fabsf(a * a + b * b - 1.0f) < 2e-7f);
        REQUIRE(state.spectral.lower[i] <= 22);
        REQUIRE(fabsf(state.spectral.lower_weight[i] +
                      state.spectral.upper_weight[i] - 1.0f) < 2e-7f);
    }
    for (i = 0; i < 1024; ++i) {
        real[i] = 100.0f * sinf(0.17f * (float)i);
    }
    REQUIRE(super_nr_fftw_fft(&state.fft, real, packed, 0) == 0);
    memset(state.spectral.frame, 0, sizeof(state.spectral.frame));
    REQUIRE(super_nr_fftw_fft(&state.fft, packed, state.spectral.frame, 1) == 0);
    for (i = 0; i < 1024; ++i) {
        REQUIRE(fabsf(real[i] - state.spectral.frame[i]) < 0.0001f);
    }
    REQUIRE(super_nr_fftw_fft(NULL, real, packed, 0) != 0);
    REQUIRE(super_nr_fftw_fft(&state.fft, real, real, 0) != 0);

    REQUIRE(super_nr_reset(&state) == SUPER_NR_OK);
    memset(previous, 0, sizeof(previous));
    for (frame = 0; frame < 3; ++frame) {
        signal(frame);
        REQUIRE(super_nr_process_spectral(&state, samples, 15, 0) == SUPER_NR_OK);
        for (i = 0; i < 512; ++i) {
            REQUIRE(fabsf(samples[i] - previous[i]) < 0.0001f);
        }
        memcpy(previous, input, sizeof(previous));
    }
    for (level = 0; level <= 30; ++level) {
        REQUIRE(super_nr_reset(&state) == SUPER_NR_OK);
        for (frame = 0; frame < 12; ++frame) {
            signal(frame);
            REQUIRE(super_nr_process(&state, samples, 1, 1000, (uint8_t)level) == SUPER_NR_OK);
            for (i = 0; i < 512; ++i) {
                REQUIRE(isfinite(samples[i]));
                REQUIRE(state.spectral.mask[i] >= state.spectral.floor[i] - 2e-7f);
                REQUIRE(state.spectral.mask[i] <= 1.000002f);
            }
        }
    }
    REQUIRE(super_nr_reset(&state) == SUPER_NR_OK);
    REQUIRE(super_nr_init(&other, NULL, NULL) == SUPER_NR_OK);
    for (frame = 0; frame < 3; ++frame) {
        signal(frame);
        super_nr_status expected = SUPER_NR_OK;
        REQUIRE(super_nr_process(&state, samples, 1, 999, 0) == expected);
        REQUIRE(super_nr_process(&other, input, 1, 999, 30) == expected);
        REQUIRE(memcmp(samples, input, sizeof(samples)) == 0);
    }
    REQUIRE(state.spectral.frames == 0);
    saved = state;
    signal(3);
    REQUIRE(super_nr_process(&state, samples, 0, 3000, 15) == SUPER_NR_OK);
    REQUIRE(memcmp(samples, input, sizeof(samples)) == 0);
    REQUIRE(memcmp(&state, &saved, sizeof(state)) == 0);
    REQUIRE(super_nr_process(&state, samples, 2, 3000, 15) == SUPER_NR_OK);
    REQUIRE(memcmp(&state, &saved, sizeof(state)) == 0);
    REQUIRE(super_nr_process_spectral(&state, samples, 31, 1) ==
            SUPER_NR_INVALID_ARGUMENT);
    REQUIRE(memcmp(&state, &saved, sizeof(state)) == 0);
    samples[0] = NAN;
    REQUIRE(super_nr_process_time(&state, samples) == SUPER_NR_NONFINITE);
    REQUIRE(memcmp(&state, &saved, sizeof(state)) == 0);
    REQUIRE(super_nr_process(&state, NULL, 1, 3000, 15) ==
            SUPER_NR_INVALID_ARGUMENT);
    REQUIRE(super_nr_reset(&state) == SUPER_NR_OK);
    memset(samples, 0, sizeof(samples));
    REQUIRE(super_nr_process_time(&state, samples) == SUPER_NR_OK);
    for (i = 0; i < 512; ++i) {
        REQUIRE(samples[i] == 0.0f);
    }
    REQUIRE(super_nr_reset(&state) == SUPER_NR_OK);
    memset(samples, 0, sizeof(samples));
    REQUIRE(super_nr_process_spectral(&state, samples, 30, 1) == SUPER_NR_OK);
    for (i = 0; i < 512; ++i) {
        REQUIRE(samples[i] == 0.0f);
    }
    REQUIRE(super_nr_reset(&state) == SUPER_NR_OK);
    for (i = 0; i < 512; ++i) {
        samples[i] = FLT_MAX;
    }
    REQUIRE(super_nr_process_spectral(&state, samples, 15, 1) == SUPER_NR_NONFINITE);
    REQUIRE(super_nr_reset(&state) == SUPER_NR_OK);
    super_nr_destroy(&state);
    REQUIRE(super_nr_init(&state, failed_fft, NULL) == SUPER_NR_OK);
    signal(0);
    REQUIRE(super_nr_process_spectral(&state, samples, 15, 1) == SUPER_NR_FFT_ERROR);
    REQUIRE(memcmp(samples, input, sizeof(samples)) == 0);
    REQUIRE(super_nr_reset(&state) == SUPER_NR_OK);
    REQUIRE(state.fft_function == failed_fft);
    printf("PASS C API, all levels, FFT/OLA, dispatch/bypass, reset and explicit errors; state=%zu bytes\n",
           sizeof(state));
    super_nr_destroy(&state);
    super_nr_destroy(&other);
    super_nr_destroy(&state);
    return 0;
}
