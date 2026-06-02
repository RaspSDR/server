#include "monitor.h"
#include <common/common.h>
#include <ft8/constants.h>

#define LOG_LEVEL LOG_INFO
#include <ft8/debug.h>

#include <stdlib.h>
#include <math.h>
#include <string.h>

static float hann_i(int i, int N)
{
    float x = sinf((float)M_PI * i / N);
    return x * x;
}

// static float hamming_i(int i, int N)
// {
//     const float a0 = (float)25 / 46;
//     const float a1 = 1 - a0;

//     float x1 = cosf(2 * (float)M_PI * i / N);
//     return a0 - a1 * x1;
// }

// static float blackman_i(int i, int N)
// {
//     const float alpha = 0.16f; // or 2860/18608
//     const float a0 = (1 - alpha) / 2;
//     const float a1 = 1.0f / 2;
//     const float a2 = alpha / 2;

//     float x1 = cosf(2 * (float)M_PI * i / N);
//     float x2 = 2 * x1 * x1 - 1; // Use double angle formula

//     return a0 - a1 * x1 + a2 * x2;
// }

static void waterfall_init(ftx_waterfall_t* me, int max_blocks, int num_bins, int time_osr, int freq_osr)
{
    size_t mag_size = (size_t)max_blocks * time_osr * freq_osr * num_bins * sizeof(me->mag[0]);
    me->max_blocks = max_blocks;
    me->num_blocks = 0;
    me->num_bins = num_bins;
    me->time_osr = time_osr;
    me->freq_osr = freq_osr;
    me->block_stride = (time_osr * freq_osr * num_bins);
    me->mag = (WF_ELEM_T*)malloc(mag_size);
    LOG(LOG_DEBUG, "Waterfall size = %zu\n", mag_size);
}

static void waterfall_free(ftx_waterfall_t* me)
{
    free(me->mag);
}

// Find next N >= n where all prime factors are in {2, 3, 5, 7}
// These sizes are efficiently handled by FFTW without needing large-prime codelets.
static int next_fftw_friendly_size(int n)
{
    while (1) {
        int m = n;
        while (m % 2 == 0) m /= 2;
        while (m % 3 == 0) m /= 3;
        while (m % 5 == 0) m /= 5;
        while (m % 7 == 0) m /= 7;
        if (m == 1) return n;
        n++;
    }
}

void monitor_init(monitor_t* me, const monitor_config_t* cfg)
{
    float slot_time;
    float symbol_period;

    if (cfg->protocol == FTX_PROTOCOL_FST4 || cfg->protocol == FTX_PROTOCOL_FST4W)
    {
        // Look up NSPS for the given T/R period
        int tr = (int)cfg->tr_period;
        int nsps = 0;
        for (int i = 0; i < FST4_NUM_TR_PERIODS; ++i)
        {
            if (kFST4_TR_periods[i] == tr)
            {
                nsps = kFST4_NSPS[i];
                break;
            }
        }
        if (nsps == 0)
        {
            // Default to 60s if invalid T/R period
            nsps = 3888;
        }
        symbol_period = (float)nsps / 12000.0f;
        slot_time = cfg->tr_period;
    }
    else if (cfg->protocol == FTX_PROTOCOL_FT4)
    {
        slot_time = FT4_SLOT_TIME;
        symbol_period = FT4_SYMBOL_PERIOD;
    }
    else
    {
        slot_time = FT8_SLOT_TIME;
        symbol_period = FT8_SYMBOL_PERIOD;
    }
    // Compute DSP parameters that depend on the sample rate
    me->block_size = (int)(cfg->sample_rate * symbol_period); // samples corresponding to one FSK symbol
    me->subblock_size = me->block_size / cfg->time_osr;
    me->nfft_logical = me->block_size * cfg->freq_osr;
    me->nfft = next_fftw_friendly_size(me->nfft_logical);
    me->fft_norm = 2.0f / me->nfft;

    // Window length: for FST4/FST4W, use block_size (1 symbol) with zero-padding
    // to achieve frequency oversampling without spanning multiple symbols.
    // FST4 uses a RECTANGULAR window because the tone spacing (1/Ts) matches the
    // symbol rate, giving perfect orthogonality (sinc null at adjacent tones).
    // A Hann window would leak -6 dB into adjacent tones, destroying tone contrast.
    // For FT8/FT4, use the full nfft samples with Hann window (traditional approach).
    int window_len = me->nfft;
    if (cfg->protocol == FTX_PROTOCOL_FST4 || cfg->protocol == FTX_PROTOCOL_FST4W)
    {
        window_len = me->block_size;
    }
    bool use_rect_window = cfg->protocol == FTX_PROTOCOL_FST4 || cfg->protocol == FTX_PROTOCOL_FST4W;

    me->window = (float*)malloc(me->nfft * sizeof(me->window[0]));
    for (int i = 0; i < me->nfft; ++i)
    {
        // For zero-padded mode, the window is applied to the LAST window_len samples
        // of last_frame (the most recent data), with zero-padding before.
        int data_offset = me->nfft - window_len;
        if (i >= data_offset)
        {
            if (use_rect_window)
            {
                me->window[i] = me->fft_norm;
            }
            else
            {
                me->window[i] = me->fft_norm * hann_i(i - data_offset, window_len);
            }
        }
        else
        {
            me->window[i] = 0; // zero-padding region (old data suppressed)
        }
    }
    me->last_frame = (float*)calloc(me->nfft, sizeof(me->last_frame[0]));

    LOG(LOG_INFO, "Block size = %d\n", me->block_size);
    LOG(LOG_INFO, "Subblock size = %d\n", me->subblock_size);
    LOG(LOG_INFO, "N_FFT = %d\n", me->nfft);

    // Allocate enough blocks to fit the entire FT8/FT4 slot in memory
    const int max_blocks = (int)(slot_time / symbol_period);
    // Keep only FFT bins in the specified frequency range (f_min/f_max)
    me->min_bin = (int)(cfg->f_min * symbol_period);
    me->max_bin = (int)(cfg->f_max * symbol_period) + 1;
    const int num_bins = me->max_bin - me->min_bin;

    waterfall_init(&me->wf, max_blocks, num_bins, cfg->time_osr, cfg->freq_osr);
    me->wf.desc = ftx_protocol_get_desc(cfg->protocol);

    me->symbol_period = symbol_period;

    me->max_mag = -120.0f;
    me->timedata = (float *) fftwf_malloc(sizeof(float) * me->nfft);
    me->freqdata = (fftwf_complex *) fftwf_malloc(sizeof(fftwf_complex) * (me->nfft / 2 + 1));
    
    me->fft_plan = fftwf_plan_dft_r2c_1d(me->nfft, me->timedata, me->freqdata, FFTW_ESTIMATE);
}

void monitor_free(monitor_t* me)
{
    waterfall_free(&me->wf);
    free(me->last_frame);
    free(me->window);
    fftwf_destroy_plan(me->fft_plan);
    fftwf_free(me->timedata);
    fftwf_free(me->freqdata);
    memset(me, 0, sizeof(*me));
}

void monitor_reset(monitor_t* me)
{
    me->wf.num_blocks = 0;
    me->max_mag = -120.0f;
}

// Compute FFT magnitudes (log wf) for a frame in the signal and update waterfall data
void monitor_process(monitor_t* me, const float* frame)
{
    // Check if we can still store more waterfall data
    if (me->wf.num_blocks >= me->wf.max_blocks)
        return;

    int offset = me->wf.num_blocks * me->wf.block_stride;
    int frame_pos = 0;

    // Loop over block subdivisions
    for (int time_sub = 0; time_sub < me->wf.time_osr; ++time_sub)
    {
        // Shift the new data into analysis frame
        for (int pos = 0; pos < me->nfft - me->subblock_size; ++pos)
        {
            me->last_frame[pos] = me->last_frame[pos + me->subblock_size];
        }
        for (int pos = me->nfft - me->subblock_size; pos < me->nfft; ++pos)
        {
            me->last_frame[pos] = frame[frame_pos];
            ++frame_pos;
        }

        // Do DFT of windowed analysis frame
        for (int pos = 0; pos < me->nfft; ++pos)
        {
            me->timedata[pos] = me->window[pos] * me->last_frame[pos];
        }
        fftwf_execute(me->fft_plan);

        // Loop over possible frequency OSR offsets
        for (int freq_sub = 0; freq_sub < me->wf.freq_osr; ++freq_sub)
        {
            for (int bin = me->min_bin; bin < me->max_bin; ++bin)
            {
                // Map logical bin to physical FFT bin (accounts for nfft padding)
                int logical_bin = (bin * me->wf.freq_osr) + freq_sub;
                int src_bin = (me->nfft == me->nfft_logical) ? logical_bin
                    : (int)(((long long)logical_bin * me->nfft + me->nfft_logical/2) / me->nfft_logical);
                if (src_bin >= me->nfft / 2 + 1) {
                    src_bin = me->nfft / 2;
                }
                float mag2 = (me->freqdata[src_bin][0] * me->freqdata[src_bin][0]) + (me->freqdata[src_bin][1] * me->freqdata[src_bin][1]);
                float db = 10.0f * log10f(1E-12f + mag2);

#ifdef WATERFALL_USE_PHASE
                // Save the magnitude in dB and phase in radians
                float phase = atan2f(me->freqdata[src_bin].i, me->freqdata[src_bin].r);
                me->wf.mag[offset].mag = db;
                me->wf.mag[offset].phase = phase;
#else
                // Scale decibels to unsigned 8-bit range and clamp the value
                int scaled = (int)(2 * db + 240);
                me->wf.mag[offset] = (scaled < 0) ? 0 : ((scaled > 255) ? 255 : scaled);
#endif
                ++offset;

                if (db > me->max_mag)
                    me->max_mag = db;
            }
        }
    }

    ++me->wf.num_blocks;
}

#ifdef WATERFALL_USE_PHASE
/**
 * Re-synthesize a signal from the frequency domain representation of a candidate.
 *
 * This function takes a candidate's frequency domain data and converts it back to a time domain
 * signal using inverse FFT. The process includes extracting frequency data around the selected 
 * candidate, applying a tapering window, and performing overlap-add synthesis.
 *
 * @param me Pointer to a monitor_t structure containing the waterfall data and FFT configurations.
 * @param candidate Pointer to an ftx_candidate_t structure representing the candidate to synthesize.
 * @param signal Output buffer to store the synthesized time domain signal.
 */
void monitor_resynth(const monitor_t* me, const ftx_candidate_t* candidate, float* signal)
{
    const int num_ifft = me->nifft;
    const int num_shift = num_ifft / 2;
    const int taper_width = 4;
    const int num_tones = 8;

    // Starting offset is 3 subblocks due to analysis buffer loading
    int offset = 1;                          // candidate->time_offset;
    offset = (offset * me->wf.time_osr) + 1; // + candidate->time_sub;
    offset = (offset * me->wf.freq_osr);     // + candidate->freq_sub;
    offset = (offset * me->wf.num_bins);     // + candidate->freq_offset;

    WF_ELEM_T* el = me->wf.mag + offset;

    // DFT frequency data - initialize to zero
    //kiss_fft_cpx freqdata[num_ifft];
    for (int i = 0; i < num_ifft; ++i)
    {
        me->freqdata[i].r = 0;
        me->freqdata[i].i = 0;
    }

    int pos = 0;
    for (int num_block = 1; num_block < me->wf.num_blocks; ++num_block)
    {
        // Extract frequency data around the selected candidate only
        for (int i = candidate->freq_offset - taper_width - 1; i < candidate->freq_offset + 8 + taper_width - 1; ++i)
        {
            if ((i >= 0) && (i < me->wf.num_bins))
            {
                int tgt_bin = (me->wf.freq_osr * (i - candidate->freq_offset) + num_ifft) % num_ifft;
                float weight = 1.0f;
                if (i < candidate->freq_offset)
                {
                    weight = ((i - candidate->freq_offset) + taper_width) / (float)taper_width;
                }
                else if (i > candidate->freq_offset + 7)
                {
                    weight = ((candidate->freq_offset + 7 - i) + taper_width) / (float)taper_width;
                }

                // Convert (dB magnitude, phase) to (real, imaginary)
                float mag = powf(10.0f, el[i].mag / 20) / 2 * weight;
                me->freqdata[tgt_bin].r = mag * cosf(el[i].phase);
                me->freqdata[tgt_bin].i = mag * sinf(el[i].phase);

                int i2 = i + me->wf.num_bins;
                tgt_bin = (tgt_bin + 1) % num_ifft;
                float mag2 = powf(10.0f, el[i2].mag / 20) / 2 * weight;
                me->freqdata[tgt_bin].r = mag2 * cosf(el[i2].phase);
                me->freqdata[tgt_bin].i = mag2 * sinf(el[i2].phase);
            }
        }

        // Compute inverse DFT and overlap-add the waveform
        kiss_fft_cpx me->timedata[num_ifft];
        kiss_fft(me->ifft_cfg, me->freqdata, me->timedata);
        for (int i = 0; i < num_ifft; ++i)
        {
            signal[pos + i] += me->timedata[i].i;
        }

        // Move to the next symbol
        el += me->wf.block_stride;
        pos += num_shift;
    }
}
#endif
