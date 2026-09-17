#ifndef _INCLUDE_MONITOR_H_
#define _INCLUDE_MONITOR_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <ft8/decode.h>
#include <fftw3.h>
#include <stddef.h>

/// Configuration options for FT4/FT8/FST4/FST4W monitor
typedef struct
{
    float f_min;             ///< Lower frequency bound for analysis
    float f_max;             ///< Upper frequency bound for analysis
    int sample_rate;         ///< Sample rate in Hertz
    int time_osr;            ///< Number of time subdivisions
    int freq_osr;            ///< Number of frequency subdivisions
    ftx_protocol_t protocol; ///< Protocol: FT4, FT8, FST4, or FST4W
    float tr_period;         ///< T/R period in seconds (used for FST4/FST4W; ignored for FT4/FT8)
} monitor_config_t;

typedef struct
{
    size_t waterfall_bytes;
    size_t window_bytes;
    size_t last_frame_bytes;
    size_t timedata_bytes;
    size_t freqdata_bytes;
    size_t shared_bytes;
    size_t frame_bytes;
    size_t total_bytes;
} monitor_memory_usage_t;

typedef struct
{
    monitor_config_t config;
    int block_size;
    int subblock_size;
    int nfft;
    float fft_norm;
    float* window;
    float* last_frame;
    float* timedata;
    fftwf_complex* freqdata;
    fftwf_plan fft_plan;
} monitor_shared_t;

/// One independently decodable waterfall frame using shared FFT processing state.
typedef struct
{
    float symbol_period;
    int min_bin;
    int max_bin;
    int block_size;
    ftx_waterfall_t wf;
    float max_mag;
    monitor_shared_t* shared;
    bool owns_shared;
} monitor_t;

/// Chunked input adapter for a monitor whose input has already been band-limited
/// below the monitor sample rate's Nyquist frequency.
typedef struct
{
    monitor_shared_t* shared;
    monitor_t* frame;
    float* block;
    int block_pos;
    int input_sample_rate;
    int decimation;
    int decimation_phase;
} monitor_stream_t;

bool monitor_get_memory_usage(const monitor_config_t* cfg, monitor_memory_usage_t* usage);
bool monitor_shared_init(monitor_shared_t* shared, const monitor_config_t* cfg);
void monitor_shared_reset(monitor_shared_t* shared);
void monitor_shared_free(monitor_shared_t* shared);
bool monitor_frame_init(monitor_t* me, monitor_shared_t* shared);
bool monitor_init(monitor_t* me, const monitor_config_t* cfg);
void monitor_reset(monitor_t* me);
void monitor_process(monitor_t* me, const float* frame);
void monitor_free(monitor_t* me);

bool monitor_stream_init(monitor_stream_t* stream, monitor_t* frame, int input_sample_rate);
bool monitor_stream_set_frame(monitor_stream_t* stream, monitor_t* frame);
int monitor_stream_process_i16(monitor_stream_t* stream, const int16_t* samples, int num_samples);
size_t monitor_stream_memory_usage(const monitor_stream_t* stream);
void monitor_stream_free(monitor_stream_t* stream);

#ifdef WATERFALL_USE_PHASE
void monitor_resynth(const monitor_t* me, const ftx_candidate_t* candidate, float* signal);
#endif

#ifdef __cplusplus
}
#endif

#endif // _INCLUDE_MONITOR_H_