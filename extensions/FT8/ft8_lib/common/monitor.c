#include "monitor.h"
#include <common/common.h>
#include <ft8/constants.h>

#define LOG_LEVEL LOG_INFO
#include <ft8/debug.h>

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    float slot_time;
    float symbol_period;
    int block_size;
    int subblock_size;
    int nfft;
    int max_blocks;
    int min_bin;
    int max_bin;
    int num_bins;
    int block_stride;
    monitor_memory_usage_t memory;
} monitor_plan_t;

static bool checked_mul_size(size_t a, size_t b, size_t* result)
{
    if (a != 0 && b > SIZE_MAX / a)
        return false;
    *result = a * b;
    return true;
}

static bool checked_add_size(size_t a, size_t b, size_t* result)
{
    if (b > SIZE_MAX - a)
        return false;
    *result = a + b;
    return true;
}

static bool monitor_make_plan(const monitor_config_t* cfg, monitor_plan_t* plan)
{
    if (cfg == NULL || plan == NULL ||
        cfg->sample_rate <= 0 || cfg->time_osr <= 0 || cfg->freq_osr <= 0 ||
        cfg->f_min < 0 || cfg->f_max <= cfg->f_min ||
        cfg->f_max > cfg->sample_rate / 2.0f)
        return false;

    memset(plan, 0, sizeof(*plan));

    if (cfg->protocol == FTX_PROTOCOL_FST4 || cfg->protocol == FTX_PROTOCOL_FST4W)
    {
        int tr_period = (int)cfg->tr_period;
        int nsps = 0;
        if ((float)tr_period != cfg->tr_period)
            return false;
        for (int i = 0; i < FST4_NUM_TR_PERIODS; ++i)
        {
            if (kFST4_TR_periods[i] == tr_period)
            {
                nsps = kFST4_NSPS[i];
                break;
            }
        }
        if (nsps == 0)
            return false;
        plan->slot_time = (float)tr_period;
        plan->symbol_period = (float)nsps / 12000.0f;
    }
    else if (cfg->protocol == FTX_PROTOCOL_FT4)
    {
        plan->slot_time = FT4_SLOT_TIME;
        plan->symbol_period = FT4_SYMBOL_PERIOD;
    }
    else if (cfg->protocol == FTX_PROTOCOL_FT8)
    {
        plan->slot_time = FT8_SLOT_TIME;
        plan->symbol_period = FT8_SYMBOL_PERIOD;
    }
    else
    {
        return false;
    }

    double block_size = cfg->sample_rate * (double)plan->symbol_period;
    double nfft = block_size * cfg->freq_osr;
    if (block_size < cfg->time_osr || block_size > INT_MAX ||
        nfft < 2 || nfft > INT_MAX)
        return false;

    plan->block_size = (int)lround(block_size);
    if (plan->block_size % cfg->time_osr != 0)
        return false;
    plan->subblock_size = plan->block_size / cfg->time_osr;
    plan->nfft = plan->block_size * cfg->freq_osr;
    if ((plan->nfft & 1) != 0)
        return false;

    plan->max_blocks = (int)(plan->slot_time / plan->symbol_period);
    plan->min_bin = (int)(cfg->f_min * plan->symbol_period);
    plan->max_bin = (int)(cfg->f_max * plan->symbol_period) + 1;
    plan->num_bins = plan->max_bin - plan->min_bin;
    if (plan->max_blocks <= 0 || plan->num_bins <= 0)
        return false;

    size_t block_stride;
    if (!checked_mul_size((size_t)cfg->time_osr, (size_t)cfg->freq_osr, &block_stride) ||
        !checked_mul_size(block_stride, (size_t)plan->num_bins, &block_stride) ||
        block_stride > INT_MAX)
        return false;
    plan->block_stride = (int)block_stride;

    size_t waterfall_elems;
    if (    !checked_mul_size((size_t)plan->max_blocks, block_stride, &waterfall_elems) ||
    !checked_mul_size(waterfall_elems, sizeof(WF_ELEM_T), &plan->memory.waterfall_bytes) ||
    !checked_mul_size((size_t)plan->nfft, sizeof(float), &plan->memory.window_bytes) ||
    !checked_mul_size((size_t)plan->nfft, sizeof(float), &plan->memory.last_frame_bytes) ||
    !checked_mul_size((size_t)plan->nfft, sizeof(float), &plan->memory.timedata_bytes) ||
    !checked_mul_size((size_t)(plan->nfft / 2 + 1), sizeof(fftwf_complex), &plan->memory.freqdata_bytes))
    return false;

    size_t shared = 0;
    const size_t shared_sizes[] = {
    plan->memory.window_bytes,
    plan->memory.last_frame_bytes,
    plan->memory.timedata_bytes,
    plan->memory.freqdata_bytes
    };
    for (size_t i = 0; i < sizeof(shared_sizes) / sizeof(shared_sizes[0]); ++i)
    {
        if (!checked_add_size(shared, shared_sizes[i], &shared))
            return false;
    }
    plan->memory.shared_bytes = shared;
    plan->memory.frame_bytes = plan->memory.waterfall_bytes;
    if (!checked_add_size(shared, plan->memory.frame_bytes, &plan->memory.total_bytes))
        return false;
    return true;
}

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

static bool waterfall_init(ftx_waterfall_t* me, const monitor_plan_t* plan, int time_osr, int freq_osr)
{
    me->max_blocks = plan->max_blocks;
    me->num_blocks = 0;
    me->num_bins = plan->num_bins;
    me->time_osr = time_osr;
    me->freq_osr = freq_osr;
    me->block_stride = plan->block_stride;
    me->mag = (WF_ELEM_T*)malloc(plan->memory.waterfall_bytes);
    LOG(LOG_DEBUG, "Waterfall size = %zu\n", plan->memory.waterfall_bytes);
    return me->mag != NULL;
}

static void waterfall_free(ftx_waterfall_t* me)
{
    free(me->mag);
}

bool monitor_get_memory_usage(const monitor_config_t* cfg, monitor_memory_usage_t* usage)
{
    monitor_plan_t plan;
    if (usage == NULL || !monitor_make_plan(cfg, &plan))
        return false;
    *usage = plan.memory;
    return true;
}

bool monitor_shared_init(monitor_shared_t* shared, const monitor_config_t* cfg)
{
    monitor_plan_t plan;
    if (shared == NULL || !monitor_make_plan(cfg, &plan))
        return false;

    memset(shared, 0, sizeof(*shared));
    shared->config = *cfg;

    // Compute DSP parameters that depend on the sample rate
    shared->block_size = plan.block_size;
    shared->subblock_size = plan.subblock_size;
    shared->nfft = plan.nfft;
    shared->fft_norm = 2.0f / shared->nfft;

    // Window length: for FST4/FST4W, use block_size (1 symbol) with zero-padding
    // to achieve frequency oversampling without spanning multiple symbols.
    // FST4 uses a RECTANGULAR window because the tone spacing (1/Ts) matches the
    // symbol rate, giving perfect orthogonality (sinc null at adjacent tones).
    // A Hann window would leak -6 dB into adjacent tones, destroying tone contrast.
    // For FT8/FT4, use the full nfft samples with Hann window (traditional approach).
    int window_len = shared->nfft;
    if (cfg->protocol == FTX_PROTOCOL_FST4 || cfg->protocol == FTX_PROTOCOL_FST4W)
    {
        window_len = shared->block_size;
    }
    bool use_rect_window = cfg->protocol == FTX_PROTOCOL_FST4 || cfg->protocol == FTX_PROTOCOL_FST4W;

    shared->window = (float*)malloc(plan.memory.window_bytes);
    shared->last_frame = (float*)calloc((size_t)shared->nfft, sizeof(shared->last_frame[0]));
    shared->timedata = (float*)fftwf_malloc(plan.memory.timedata_bytes);
    shared->freqdata = (fftwf_complex*)fftwf_malloc(plan.memory.freqdata_bytes);
    if (shared->window == NULL || shared->last_frame == NULL || shared->timedata == NULL ||
        shared->freqdata == NULL)
    {
        monitor_shared_free(shared);
        return false;
    }

    for (int i = 0; i < shared->nfft; ++i)
    {
        // For zero-padded mode, the window is applied to the LAST window_len samples
        // of last_frame (the most recent data), with zero-padding before.
        int data_offset = shared->nfft - window_len;
        if (i >= data_offset)
        {
            if (use_rect_window)
            {
                shared->window[i] = shared->fft_norm;
            }
            else
            {
                shared->window[i] = shared->fft_norm * hann_i(i - data_offset, window_len);
            }
        }
        else
        {
            shared->window[i] = 0;
        }
    }
    LOG(LOG_INFO, "Block size = %d\n", shared->block_size);
    LOG(LOG_INFO, "Subblock size = %d\n", shared->subblock_size);

    shared->fft_plan = fftwf_plan_dft_r2c_1d(shared->nfft, shared->timedata, shared->freqdata, FFTW_ESTIMATE);
    if (shared->fft_plan == NULL)
    {
        monitor_shared_free(shared);
        return false;
    }

    LOG(LOG_INFO, "N_FFT = %d\n", shared->nfft);
    return true;
}

void monitor_shared_reset(monitor_shared_t* shared)
{
    if (shared != NULL && shared->last_frame != NULL)
        memset(shared->last_frame, 0, (size_t)shared->nfft * sizeof(shared->last_frame[0]));
}

void monitor_shared_free(monitor_shared_t* shared)
{
    if (shared == NULL)
        return;
    fftwf_destroy_plan(shared->fft_plan);
    fftwf_free(shared->freqdata);
    fftwf_free(shared->timedata);
    free(shared->last_frame);
    free(shared->window);
    memset(shared, 0, sizeof(*shared));
}

bool monitor_frame_init(monitor_t* me, monitor_shared_t* shared)
{
    monitor_plan_t plan;
    if (me == NULL || shared == NULL || shared->fft_plan == NULL ||
        !monitor_make_plan(&shared->config, &plan))
        return false;

    memset(me, 0, sizeof(*me));
    me->shared = shared;
    me->block_size = plan.block_size;
    me->min_bin = plan.min_bin;
    me->max_bin = plan.max_bin;

    if (!waterfall_init(&me->wf, &plan, shared->config.time_osr, shared->config.freq_osr))
    {
        memset(me, 0, sizeof(*me));
        return false;
    }
    me->wf.desc = ftx_protocol_get_desc(shared->config.protocol);
    me->symbol_period = plan.symbol_period;
    me->max_mag = -120.0f;
    return true;
}

bool monitor_init(monitor_t* me, const monitor_config_t* cfg)
{
    if (me == NULL)
        return false;
    monitor_shared_t* shared = (monitor_shared_t*)malloc(sizeof(*shared));
    if (shared == NULL)
        return false;
    if (!monitor_shared_init(shared, cfg) || !monitor_frame_init(me, shared))
    {
        monitor_shared_free(shared);
        free(shared);
        return false;
    }
    me->owns_shared = true;
    return true;
}

void monitor_reset(monitor_t* me)
{
    me->wf.num_blocks = 0;
    me->max_mag = -120.0f;
    monitor_shared_reset(me->shared);
}

void monitor_free(monitor_t* me)
{
    if (me == NULL)
        return;
    waterfall_free(&me->wf);
    if (me->owns_shared)
    {
        monitor_shared_free(me->shared);
        free(me->shared);
    }
    memset(me, 0, sizeof(*me));
}

bool monitor_stream_init(monitor_stream_t* stream, monitor_t* frame, int input_sample_rate)
{
    if (stream == NULL || frame == NULL || frame->shared == NULL ||
        input_sample_rate <= 0 ||
        input_sample_rate % frame->shared->config.sample_rate != 0)
        return false;

    memset(stream, 0, sizeof(*stream));
    stream->shared = frame->shared;
    stream->input_sample_rate = input_sample_rate;
    stream->decimation = input_sample_rate / frame->shared->config.sample_rate;
    stream->block = (float*)malloc((size_t)frame->block_size * sizeof(stream->block[0]));
    if (stream->block == NULL)
    {
        memset(stream, 0, sizeof(*stream));
        return false;
    }
    return monitor_stream_set_frame(stream, frame);
}

bool monitor_stream_set_frame(monitor_stream_t* stream, monitor_t* frame)
{
    if (stream == NULL || stream->block == NULL || frame == NULL ||
        frame->shared != stream->shared)
        return false;

    stream->frame = frame;
    stream->block_pos = 0;
    stream->decimation_phase = 0;
    monitor_reset(frame);
    return true;
}

int monitor_stream_process_i16(monitor_stream_t* stream, const int16_t* samples, int num_samples)
{
    if (stream == NULL || stream->frame == NULL || stream->block == NULL ||
        samples == NULL || num_samples < 0)
        return -1;

    int blocks_processed = 0;
    for (int i = 0; i < num_samples; ++i)
    {
        if (stream->decimation_phase == 0)
        {
            stream->block[stream->block_pos++] = samples[i] / 32768.0f;
            if (stream->block_pos == stream->frame->block_size)
            {
                monitor_process(stream->frame, stream->block);
                stream->block_pos = 0;
                ++blocks_processed;
            }
        }

        ++stream->decimation_phase;
        if (stream->decimation_phase == stream->decimation)
            stream->decimation_phase = 0;
    }
    return blocks_processed;
}

size_t monitor_stream_memory_usage(const monitor_stream_t* stream)
{
    if (stream == NULL || stream->frame == NULL || stream->block == NULL)
        return 0;
    return (size_t)stream->frame->block_size * sizeof(stream->block[0]);
}

void monitor_stream_free(monitor_stream_t* stream)
{
    if (stream == NULL)
        return;
    free(stream->block);
    memset(stream, 0, sizeof(*stream));
}

// Compute FFT magnitudes (log wf) for a frame in the signal and update waterfall data
void monitor_process(monitor_t* me, const float* frame)
{
    monitor_shared_t* shared = me->shared;
    // Check if we can still store more waterfall data
    if (shared == NULL || me->wf.num_blocks >= me->wf.max_blocks)
        return;

    int offset = me->wf.num_blocks * me->wf.block_stride;
    int frame_pos = 0;

    // Loop over block subdivisions
    for (int time_sub = 0; time_sub < me->wf.time_osr; ++time_sub)
    {
        // Shift the new data into analysis frame
        for (int pos = 0; pos < shared->nfft - shared->subblock_size; ++pos)
        {
            shared->last_frame[pos] = shared->last_frame[pos + shared->subblock_size];
        }
        for (int pos = shared->nfft - shared->subblock_size; pos < shared->nfft; ++pos)
        {
            shared->last_frame[pos] = frame[frame_pos];
            ++frame_pos;
        }

        // Do DFT of windowed analysis frame
        for (int pos = 0; pos < shared->nfft; ++pos)
        {
            shared->timedata[pos] = shared->window[pos] * shared->last_frame[pos];
        }
        fftwf_execute(shared->fft_plan);

        // Loop over possible frequency OSR offsets
        for (int freq_sub = 0; freq_sub < me->wf.freq_osr; ++freq_sub)
        {
            for (int bin = me->min_bin; bin < me->max_bin; ++bin)
            {
                int src_bin = (bin * me->wf.freq_osr) + freq_sub;
                float mag2 = (shared->freqdata[src_bin][0] * shared->freqdata[src_bin][0]) +
                    (shared->freqdata[src_bin][1] * shared->freqdata[src_bin][1]);
                float db = 10.0f * log10f(1E-12f + mag2);

#ifdef WATERFALL_USE_PHASE
                // Save the magnitude in dB and phase in radians
                float phase = atan2f(shared->freqdata[src_bin][1], shared->freqdata[src_bin][0]);
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
    const int num_ifft = 64;
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
    fftwf_complex freqdata[num_ifft];
    for (int i = 0; i < num_ifft; ++i)
    {
        freqdata[i][0] = 0;
        freqdata[i][1] = 0;
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
                freqdata[tgt_bin][0] = mag * cosf(el[i].phase);
                freqdata[tgt_bin][1] = mag * sinf(el[i].phase);

                int i2 = i + me->wf.num_bins;
                tgt_bin = (tgt_bin + 1) % num_ifft;
                float mag2 = powf(10.0f, el[i2].mag / 20) / 2 * weight;
                freqdata[tgt_bin][0] = mag2 * cosf(el[i2].phase);
                freqdata[tgt_bin][1] = mag2 * sinf(el[i2].phase);
            }
        }

        // Compute inverse DFT and overlap-add the waveform
        fftwf_complex timedata[num_ifft];
        fftwf_plan ifft_plan = fftwf_plan_dft_1d(num_ifft, freqdata, timedata, FFTW_BACKWARD, FFTW_ESTIMATE);
        if (ifft_plan == NULL)
            return;
        fftwf_execute(ifft_plan);
        fftwf_destroy_plan(ifft_plan);
        for (int i = 0; i < num_ifft; ++i)
        {
            signal[pos + i] += timedata[i][1];
        }

        // Move to the next symbol
        el += me->wf.block_stride;
        pos += num_shift;
    }
}
#endif
