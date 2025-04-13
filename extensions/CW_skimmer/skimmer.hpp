#include "csdr/ringbuffer.hpp"
#include "csdr/cw.hpp"
#include "fftw3.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define showDbg 0 // 1: Show debug information

#define USE_NEIGHBORS  0 // 1: Subtract neighbors from each FFT bucket
#define USE_AVG_BOTTOM 0 // 1: Subtract average value from each bucket
#define USE_AVG_RATIO  0 // 1: Divide each bucket by average value
#define USE_THRESHOLD  1 // 1: Convert each bucket to 0.0/1.0 values

#define MAX_SCALES   (16)
#define MAX_CHANNELS (128)
#define MAX_INPUT    (MAX_CHANNELS * 2)
#define INPUT_STEP   (MAX_INPUT) // MAX_INPUT/4
#define AVG_SECONDS  (3)
#define NEIGH_WEIGHT (0.5)
#define THRES_WEIGHT (6.0)

#include <functional>
typedef std::function<void(int, char, int)> OutputCallback;

typedef enum {
    PWR_CALC_AVG_RATIO,  // Divide each bucket by average value
    PWR_CALC_AVG_BOTTOM, // Subtract average value from each bucket
    PWR_CALC_THRESHOLD,  // Convert each bucket to 0.0/1.0 values
} PwrCalc_t;

class CwSkimmer {
public:
    CwSkimmer(int sampleRate) : pwr_calc(PWR_CALC_AVG_RATIO), filter_neighbors(false), sampleRate(sampleRate) {
        // initialize fftw
        fft = fftwf_plan_dft_r2c_1d(MAX_INPUT, fftIn, fftOut, FFTW_MEASURE);

        // Create and connect CSDR objects, clear output state
        for (int j = 0; j < MAX_CHANNELS; ++j) {
            in[j] = new Csdr::Ringbuffer<float>(sampleRate);
            inReader[j] = new Csdr::RingbufferReader<float>(in[j]);
            out[j] = new Csdr::Ringbuffer<unsigned char>(printChars * 4);
            outReader[j] = new Csdr::RingbufferReader<unsigned char>(out[j]);
            cwDecoder[j] = new Csdr::CwDecoder<float>(sampleRate, false);
            cwDecoder[j]->setReader(inReader[j]);
            cwDecoder[j]->setWriter(out[j]);
            outState[j] = ' ';
        }

        remains = 0;
        avgPower = 4.0;
    }

    ~CwSkimmer() {
        // Release FFTW3 resources
        fftwf_destroy_plan(fft);

        // Release CSDR resources
        for (int j = 0; j < MAX_CHANNELS; ++j) {
            delete outReader[j];
            delete out[j];
            delete cwDecoder[j];
            delete inReader[j];
            delete in[j];
        }
    }

    void reset() {
        // Final printout
        for (int i = 0; i < MAX_CHANNELS; i++) {
            cwDecoder[i]->reset();
            outState[i] = ' ';
        }
        remains = 0;
        avgPower = 4.0;
    }

    void SetParams(PwrCalc_t pwr_calc, bool filter_neighbors) {
        this->pwr_calc = pwr_calc;
        this->filter_neighbors = filter_neighbors;
    }

    void SetCallback(OutputCallback callback) {
        this->callback = callback;
    }

    void AddSamples(float* sample, size_t count) {
        while (count > 0) {
            if (count >= MAX_INPUT - remains) {
                // more samples than we can fit in the buffer
                memcpy(dataBuf + remains, sample, (MAX_INPUT - remains) * sizeof(float));
                count -= MAX_INPUT - remains;
                sample += MAX_INPUT - remains;
                process();
                remains = 0;
            }
            else {
                // we can fit the samples in the buffer
                memcpy(dataBuf + remains, sample, count * sizeof(float));

                remains += count;
                count = 0;
            }
        }
    }

    void AddSamples(int16_t* sample, size_t count) {
        while (count > 0) {
            if (count >= MAX_INPUT - remains) {
                // same or more samples than we can fit in the buffer
                for (size_t i = 0; i < MAX_INPUT - remains; i++) {
                    dataBuf[remains + i] = (float)sample[i] / 32768.0;
                }
                count -= MAX_INPUT - remains;
                sample += MAX_INPUT - remains;
                process();
                remains = 0;
            }
            else {
                // we can fit the samples in the buffer
                for (size_t i = 0; i < count; i++) {
                    dataBuf[remains + i] = sample[i] / 32768.0;
                }

                remains += count;
                count = 0;
            }
        }
    }

private:
    float avgPower;
    size_t remains;

    PwrCalc_t pwr_calc;
    bool filter_neighbors;
    unsigned int sampleRate;

    unsigned int printChars = 8; // Number of characters to print at once

    fftwf_complex fftOut[MAX_INPUT];
    float dataBuf[MAX_INPUT];
    float fftIn[MAX_INPUT];
    fftwf_plan fft;

    Csdr::Ringbuffer<float>* in[MAX_CHANNELS];
    Csdr::RingbufferReader<float>* inReader[MAX_CHANNELS];
    Csdr::Ringbuffer<unsigned char>* out[MAX_CHANNELS];
    Csdr::RingbufferReader<unsigned char>* outReader[MAX_CHANNELS];
    Csdr::CwDecoder<float>* cwDecoder[MAX_CHANNELS];
    unsigned int outState[MAX_CHANNELS];

    OutputCallback callback;

    // Hamming window function
    float hamming(unsigned int x, unsigned int size) {
        return (0.54 - 0.46 * cosf((2.0 * M_PI * x) / (size - 1)));
    }

    // when there is enough samples
    void process() {
        // Debug output gets accumulated here
        char dbgOut[MAX_CHANNELS + 16];

        float maxPower, accPower;
        int j, i, k, n;

        // Apply Hamming window
        double hk = 2.0 * M_PI / (MAX_INPUT - 1);
        for (j = 0; j < MAX_INPUT; ++j)
            fftIn[j] = dataBuf[j] * (0.54 - 0.46 * cos(j * hk));

        // Shift input data
        // remains = MAX_INPUT-INPUT_STEP;
        // memcpy(dataBuf, dataBuf+INPUT_STEP, remains*sizeof(float));

        // Compute FFT
        fftwf_execute(fft);

        // Go to magnitudes
        for (j = 0; j < MAX_INPUT / 2; ++j)
            fftOut[j][0] = fftOut[j][1] = sqrt(fftOut[j][0] * fftOut[j][0] + fftOut[j][1] * fftOut[j][1]);

        // Filter out spurs
        if (filter_neighbors) {
            fftOut[MAX_INPUT / 2 - 1][0] = fmax(0.0, fftOut[MAX_INPUT / 2 - 1][1] - NEIGH_WEIGHT * fftOut[MAX_INPUT / 2 - 2][1]);
            fftOut[0][0] = fmax(0.0, fftOut[0][1] - NEIGH_WEIGHT * fftOut[1][1]);
            for (j = 1; j < MAX_INPUT / 2 - 1; ++j)
                fftOut[j][0] = fmax(0.0, fftOut[j][1] - 0.5 * NEIGH_WEIGHT * (fftOut[j - 1][1] + fftOut[j + 1][1]));
        }

        struct
        {
            float power;
            int count;
        } scales[MAX_SCALES];

        // Sort buckets into scales
        memset(scales, 0, sizeof(scales));
        for (j = 0, maxPower = 0.0; j < MAX_INPUT / 2; ++j) {
            float v = fftOut[j][0];
            int scale = floor(log(v));
            scale = scale < 0 ? 0 : scale + 1 >= MAX_SCALES ? MAX_SCALES - 1
                                                            : scale + 1;
            maxPower = fmax(maxPower, v);
            scales[scale].power += v;
            scales[scale].count++;
        }

        // Find most populated scales and use them for ground power
        for (i = 0, n = 0, accPower = 0.0; i < MAX_SCALES - 1; ++i) {
            // Look for the next most populated scale
            for (k = i, j = i + 1; j < MAX_SCALES; ++j)
                if (scales[j].count > scales[k].count)
                    k = j;
            // If found, swap with current one
            if (k != i) {
                float v = scales[k].power;
                j = scales[k].count;
                scales[k] = scales[i];
                scales[i].power = v;
                scales[i].count = j;
            }
            // Keep track of the total number of buckets
            accPower += scales[i].power;
            n += scales[i].count;
            // Stop when we collect 1/2 of all buckets
            if (n >= MAX_INPUT / 2 / 2)
                break;
        }

        // fprintf(stderr, "accPower = %f (%d buckets, %d%%)\n", accPower/n, i+1, 100*n*2/MAX_INPUT);

        // Maintain rolling average over AVG_SECONDS
        accPower /= n;
        avgPower += (accPower - avgPower) * INPUT_STEP / sampleRate / AVG_SECONDS;

        // Decode by channel
        for (j = i = k = n = 0, accPower = 0.0; j < MAX_INPUT / 2; ++j, ++n) {
            float power = fftOut[j][0];

            // If accumulated enough FFT buckets for a channel...
            if (k >= MAX_INPUT / 2) {
                switch(pwr_calc) {
                case PWR_CALC_AVG_RATIO:
                    // Divide channel signal by the average power
                    accPower = fmaxf(1.0, accPower / fmaxf(avgPower, 0.000001));
                    break;
                case PWR_CALC_AVG_BOTTOM:
                    // Subtract average power from the channel signal
                    accPower = fmaxf(0.0, accPower - avgPower);
                    break;
                case PWR_CALC_THRESHOLD:
                    // Convert channel signal to 1/0 values based on threshold
                    accPower = accPower >= avgPower * THRES_WEIGHT ? 1.0f : 0.0f;
                    break;
                }

                dbgOut[i] = accPower < 0.5 ? '.' : '0' + round(fmax(fmin(accPower / maxPower * 10.0, 9.0), 0.0));

                // If CW input buffer can accept samples...
                if (in[i]->writeable() >= INPUT_STEP) {
                    // Fill input buffer with computed signal power
                    float* dst = in[i]->getWritePointer();
                    for (int j = 0; j < INPUT_STEP; ++j)
                        dst[j] = accPower;
                    in[i]->advance(INPUT_STEP);

                    // Process input for the current channel
                    while (cwDecoder[i]->canProcess())
                        cwDecoder[i]->process();

                    // Print output
                    printOutput(i, i * sampleRate / 2 / MAX_CHANNELS, printChars);
                }

                // Start on a new channel
                accPower = 0.0;
                k -= MAX_INPUT / 2;
                i += 1;
                n = 0;
            }

            // Maximize channel signal power
            accPower = fmax(accPower, power);
            k += MAX_CHANNELS;
        }

        // Print debug information to the stderr
        dbgOut[i] = '\0';
        if (showDbg)
            fprintf(stderr, "%s (%.2f, %.2f)\n", dbgOut, avgPower, maxPower);
    }

    // Print output from ith decoder
    void
    printOutput(int i, unsigned int freq, unsigned int printChars) {
        // Must have a minimum of printChars
        size_t n = outReader[i]->available();
        if (n < printChars) return;
        int wpm = cwDecoder[i]->getWPM();

        // Print characters
        unsigned char* p = outReader[i]->getReadPointer();
        for (size_t j = 0; j < n; ++j) {
            switch (outState[i] & 0xFF) {
            case '\0':
                // Print character
                callback(freq, p[j], wpm);
                // Once we encounter a space, wait for stray characters
                if (p[j] == ' ') outState[i] = p[j];
                break;
            case ' ':
                // If possible error, save it in state, else print and reset state
                if (strchr("TEI ", p[j]))
                    outState[i] = p[j];
                else {
                    callback(freq, p[j], wpm);
                    outState[i] = '\0';
                }
                break;
            default:
                // If likely error, skip it, else print and reset state
                if (strchr("TEI ", p[j]))
                    outState[i] = (outState[i] << 8) | p[j];
                else {
                    for (int k = 24; k >= 0; k -= 8)
                        if ((outState[i] >> k) & 0xFF)
                            callback(freq, (outState[i] >> k) & 0xFF, wpm);
                    callback(freq, p[j], wpm);
                    outState[i] = '\0';
                }
                break;
            }
        }

        // Done printing
        outReader[i]->advance(n);
    }
};
