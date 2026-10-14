#include "audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static int16_t pcm[SUPER_NR_HOP], input[SUPER_NR_HOP];
static super_nr reference;

int main()
{
    SuperNR audio;
    REQUIRE(audio.process(SUPER_NR_HOP, pcm, 2400, 15) == SUPER_NR_INVALID_ARGUMENT);
    REQUIRE(audio.init(0) == SUPER_NR_INVALID_ARGUMENT);
    const float rates[] = { 12000, 20250, 40000 };
    for (unsigned rate = 0; rate < sizeof(rates) / sizeof(rates[0]); rate++) {
        REQUIRE(audio.init(rates[rate]) == SUPER_NR_OK);
        REQUIRE(super_nr_init_rate(&reference, rates[rate], NULL, NULL) == SUPER_NR_OK);
        double energy[31] = {};
        for (int level = 0; level <= 30; level++) {
            REQUIRE(audio.reset() == SUPER_NR_OK);
            REQUIRE(super_nr_reset(&reference) == SUPER_NR_OK);
            float block[SUPER_NR_HOP];
            double difference = 0;
            for (int frame = 0; frame < 12; frame++) {
                for (int i = 0; i < SUPER_NR_HOP; i++) {
                    pcm[i] = input[i] = (int16_t) (8000 * sinf(
                        (frame * SUPER_NR_HOP + i) * 0.2f) + 1200 * cosf(i * 0.713f));
                    block[i] = pcm[i];
                }
                REQUIRE(audio.process(SUPER_NR_HOP, pcm, 2400, level) == SUPER_NR_OK);
                REQUIRE(super_nr_process(&reference, block, 1, 2400, level) == SUPER_NR_OK);
                for (int i = 0; i < SUPER_NR_HOP; i++) {
                    int16_t expected = block[i] >= 32767 ? 32767 :
                        block[i] <= -32768 ? -32768 : (int16_t) block[i];
                    REQUIRE(pcm[i] == expected);
                    difference += fabs((double) pcm[i] - input[i]);
                    energy[level] += (double) pcm[i] * pcm[i];
                }
            }
            REQUIRE(difference > 1000);
        }
        REQUIRE(energy[30] < energy[0]);
        const int bandwidths[] = { 999, 1000 };
        const float gains[] = { powf(10.0f, -30.0f / 20.0f), 1.0f, powf(10.0f, 30.0f / 20.0f) };
        for (unsigned path = 0; path < 2; path++) {
            REQUIRE(audio.reset() == SUPER_NR_OK);
            REQUIRE(super_nr_reset(&reference) == SUPER_NR_OK);
            bool clipped = false;
            for (int frame = 0; frame < 12; frame++) {
                float block[SUPER_NR_HOP];
                float gain = gains[frame % 3];
                for (int i = 0; i < SUPER_NR_HOP; i++) {
                    pcm[i] = (int16_t) (20000 * sinf((frame * SUPER_NR_HOP + i) * 0.2f));
                    block[i] = pcm[i];
                }
                REQUIRE(audio.process(SUPER_NR_HOP, pcm, bandwidths[path], 15, gain) == SUPER_NR_OK);
                REQUIRE(super_nr_process(&reference, block, 1, bandwidths[path], 15) == SUPER_NR_OK);
                for (int i = 0; i < SUPER_NR_HOP; i++) {
                    float scaled = block[i] * gain;
                    int16_t expected = scaled >= 32767 ? 32767 :
                        scaled <= -32768 ? -32768 : (int16_t) scaled;
                    REQUIRE(pcm[i] == expected);
                    clipped |= scaled >= 32767 || scaled <= -32768;
                }
            }
            REQUIRE(clipped);
        }
        REQUIRE(audio.reset() == SUPER_NR_OK);
        memset(pcm, 0, sizeof(pcm));
        for (int frame = 0; frame < 4; frame++) {
            REQUIRE(audio.process(SUPER_NR_HOP, pcm, 500, 30) == SUPER_NR_OK);
            for (int i = 0; i < SUPER_NR_HOP; i++)
                REQUIRE(pcm[i] == 0);
        }
        for (int frame = 0; frame < 12; frame++) {
            for (int i = 0; i < SUPER_NR_HOP; i++)
                pcm[i] = i & 1 ? -32768 : 32767;
            REQUIRE(audio.process(SUPER_NR_HOP, pcm, frame & 1 ? 999 : 1000, 15) == SUPER_NR_OK);
        }
        memcpy(input, pcm, sizeof(pcm));
        REQUIRE(audio.process(511, pcm, 2400, 15) == SUPER_NR_INVALID_ARGUMENT);
        REQUIRE(audio.process(512, pcm, 2400, 31) == SUPER_NR_INVALID_ARGUMENT);
        REQUIRE(audio.process(512, pcm, 2400, -1) == SUPER_NR_INVALID_ARGUMENT);
        REQUIRE(audio.process(512, pcm, 2400, 15, NAN) == SUPER_NR_INVALID_ARGUMENT);
        REQUIRE(audio.process(512, pcm, 2400, 15, INFINITY) == SUPER_NR_INVALID_ARGUMENT);
        REQUIRE(audio.process(512, pcm, 2400, 15, -1) == SUPER_NR_INVALID_ARGUMENT);
        REQUIRE(audio.process(512, pcm, 2400, 15, 33) == SUPER_NR_INVALID_ARGUMENT);
        REQUIRE(memcmp(input, pcm, sizeof(pcm)) == 0);
        audio.destroy();
        super_nr_destroy(&reference);
    }
    puts("PASS Super NR PCM adapter: all 31 levels at 12/20.25/40 kHz, gain scaling/clipping in both paths, silence, full scale, path switches, lifecycle");
}
