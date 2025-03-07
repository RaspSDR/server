#include "csdr/ringbuffer.hpp"
#include "csdr/cw.hpp"
#include "fftw3.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define USE_NEIGHBORS  0 // 1: Subtract neighbors from each FFT bucket
#define USE_AVG_BOTTOM 0 // 1: Subtract average value from each bucket
#define USE_AVG_RATIO  0 // 1: Divide each bucket by average value
#define USE_THRESHOLD  1 // 1: Convert each bucket to 0.0/1.0 values

#define sampleRate   12000 // Input audio sampling rate
#define MAX_SCALES   (16)
#define MAX_CHANNELS (sampleRate / 2 / 100)
#define MAX_INPUT    (MAX_CHANNELS * 2)
#define AVG_SECONDS  (3)
#define NEIGH_WEIGHT (0.5)
#define THRES_WEIGHT (6.0)

#include <functional>
typedef std::function<void(int, char)> OutputCallback;

class CwSkimmer {
public:
    CwSkimmer() {
        // initialize fftw
        fft = fftwf_plan_dft_r2c_1d(MAX_INPUT, fftIn, fftOut, FFTW_ESTIMATE);

        // Allocate CSDR object storage
        in = new Csdr::Ringbuffer<float>*[MAX_CHANNELS];
        inReader = new Csdr::RingbufferReader<float>*[MAX_CHANNELS];
        out = new Csdr::Ringbuffer<unsigned char>*[MAX_CHANNELS];
        outReader = new Csdr::RingbufferReader<unsigned char>*[MAX_CHANNELS];
        cwDecoder = new Csdr::CwDecoder<float>*[MAX_CHANNELS];
        outState = new unsigned int[MAX_CHANNELS];

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

        // Release CSDR object storage
        delete[] in;
        delete[] inReader;
        delete[] out;
        delete[] outReader;
        delete[] cwDecoder;
        delete[] outState;
    }

    void flush() {
        // Final printout
        for (int i = 0; i < MAX_CHANNELS; i++) {
            printOutput(i, i * sampleRate / 2 / MAX_CHANNELS, 1);
            outState[i] = ' ';
        }
        remains = 0;
        avgPower = 4.0;
    }

    void SetCallback(OutputCallback callback) {
        this->callback = callback;

        // reset the state as well
        for (int i = 0; i < MAX_CHANNELS; i++) {
            outState[i] = ' ';
        }

        remains = 0;
        avgPower = 4.0;
    }

    void AddSamples(float* sample, size_t count) {
        while (count > 0) {
            if (count > MAX_INPUT - remains) {
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
                if (remains == 0) {
                    process();
                    remains = 0;
                }
                break;
            }
        }
    }

    void AddSamples(int16_t* sample, size_t count) {
        while (count > 0) {
            if (count > MAX_INPUT - remains) {
                // more samples than we can fit in the buffer
                for (size_t i = 0; i < MAX_INPUT - remains; i++) {
                    dataBuf[remains + i] = sample[i] / 32768.0;
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
                if (remains == 0) {
                    process();
                    remains = 0;
                }
                break;
            }
        }
    }

private:
    float avgPower;
    size_t remains;

    unsigned int printChars = 8; // Number of characters to print at once

    fftwf_complex fftOut[MAX_INPUT];
    short dataIn[MAX_INPUT];
    float dataBuf[MAX_INPUT];
    float fftIn[MAX_INPUT];
    fftwf_plan fft;

    Csdr::Ringbuffer<float>** in;
    Csdr::RingbufferReader<float>** inReader;
    Csdr::Ringbuffer<unsigned char>** out;
    Csdr::RingbufferReader<unsigned char>** outReader;
    Csdr::CwDecoder<float>** cwDecoder;
    unsigned int* outState;

    OutputCallback callback;

    // Hamming window function
    float hamming(unsigned int x, unsigned int size) {
        return (0.54 - 0.46 * cosf((2.0 * M_PI * x) / (size - 1)));
    }

    // when there is enough samples
    void process() {
        float maxPower, accPower;
        // Apply Hamming window
        const float hk = 2.0 * M_PI / (MAX_INPUT - 1);
        for (int j = 0; j < MAX_INPUT; ++j)
            fftIn[j] = dataBuf[j] * (0.54 - 0.46 * cosf(j * hk));

        // Compute FFT
        fftwf_execute(fft);

        // Go to magnitudes
        for (int j = 0; j < MAX_INPUT / 2; ++j)
            fftOut[j][0] = fftOut[j][1] = sqrtf(fftOut[j][0] * fftOut[j][0] + fftOut[j][1] * fftOut[j][1]);

        // Filter out spurs
#if USE_NEIGHBORS
        fftOut[MAX_INPUT / 2 - 1][0] = fmaxf(0.0, fftOut[MAX_INPUT / 2 - 1][1] - NEIGH_WEIGHT * fftOut[MAX_INPUT / 2 - 2][1]);
        fftOut[0][0] = fmaxf(0.0, fftOut[0][1] - NEIGH_WEIGHT * fftOut[1][1]);
        for (int j = 1; j < MAX_INPUT / 2 - 1; ++j)
            fftOut[j][0] = fmaxf(0.0, fftOut[j][1] - 0.5 * NEIGH_WEIGHT * (fftOut[j - 1][1] + fftOut[j + 1][1]));
#endif

        struct
        {
            float power;
            int count;
        } scales[MAX_SCALES];

        // Sort buckets into scales
        memset(scales, 0, sizeof(scales));
        maxPower = 0.0;
        for (int j = 0; j < MAX_INPUT / 2; ++j) {
            float v = fftOut[j][0];
            int scale = floorf(logf(v));
            scale = scale < 0 ? 0 : scale + 1 >= MAX_SCALES ? MAX_SCALES - 1
                                                            : scale + 1;
            maxPower = fmaxf(maxPower, v);
            scales[scale].power += v;
            scales[scale].count++;
        }

        // Find most populated scales and use them for ground power
        int n = 0;
        accPower = 0.0;
        for (int i = 0; i < MAX_SCALES - 1; ++i) {
            // Look for the next most populated scale
            int k, j;
            for (k = i, j = i + 1; j < MAX_SCALES; ++j)
                if (scales[j].count > scales[k].count) k = j;
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
            if (n >= MAX_INPUT / 2 / 2) break;
        }

        // fprintf(stderr, "accPower = %f (%d buckets, %d%%)\n", accPower/n, i+1, 100*n*2/MAX_INPUT);

        // Maintain rolling average over AVG_SECONDS
        accPower /= n;
        avgPower += (accPower - avgPower) * MAX_INPUT / sampleRate / AVG_SECONDS;

        // Decode by channel
        int i, j, k;
        for (j = i = k = n = 0, accPower = 0.0; j < MAX_INPUT / 2; ++j, ++n) {
            float power = fftOut[j][0];

            // If accumulated enough FFT buckets for a channel...
            if (k >= MAX_INPUT / 2) {
#if USE_AVG_RATIO
                // Divide channel signal by the average power
                accPower = fmaxf(1.0, accPower / fmaxf(avgPower, 0.000001));
#elif USE_AVG_BOTTOM
                // Subtract average power from the channel signal
                accPower = fmaxf(0.0, accPower - avgPower);
#elif USE_THRESHOLD
                // Convert channel signal to 1/0 values based on threshold
                accPower = accPower >= avgPower * THRES_WEIGHT ? 1.0 : 0.0;
#endif

                // If CW input buffer can accept samples...
                if (in[i]->writeable() >= MAX_INPUT) {
                    // Fill input buffer with computed signal power
                    float* dst = in[i]->getWritePointer();
                    for (int j = 0; j < MAX_INPUT; ++j) dst[j] = accPower;
                    in[i]->advance(MAX_INPUT);

                    // Process input for the current channel
                    while (cwDecoder[i]->canProcess()) cwDecoder[i]->process();

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
            accPower = fmaxf(accPower, power);
            k += MAX_CHANNELS;
        }
    }

    // Print output from ith decoder
    void
    printOutput(int i, unsigned int freq, unsigned int printChars) {
        // Must have a minimum of printChars
        size_t n = outReader[i]->available();
        if (n < printChars) return;

        // Print characters
        unsigned char* p = outReader[i]->getReadPointer();
        for (size_t j = 0; j < n; ++j) {
            switch (outState[i] & 0xFF) {
            case '\0':
                // Print character
                callback(freq, p[j]);
                // Once we encounter a space, wait for stray characters
                if (p[j] == ' ') outState[i] = p[j];
                break;
            case ' ':
                // If possible error, save it in state, else print and reset state
                if (strchr("TEI ", p[j]))
                    outState[i] = p[j];
                else {
                    callback(freq, p[j]);
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
                            callback(freq, (outState[i] >> k) & 0xFF);
                    callback(freq, p[j]);
                    outState[i] = '\0';
                }
                break;
            }
        }

        // Done printing
        outReader[i]->advance(n);
    }
};
