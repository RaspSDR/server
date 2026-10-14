#ifndef SUPER_NR_H
#define SUPER_NR_H

#include <stdint.h>
#include <fftw3.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SUPER_NR_HOP = 512,
    SUPER_NR_FFT_SIZE = 1024,
    SUPER_NR_BANDS = 24,
    SUPER_NR_POWERS = 536,
    SUPER_NR_REFERENCE_RATE = 40000,
    SUPER_NR_DEFAULT_LEVEL = 15
};

typedef enum {
    SUPER_NR_OK = 0,
    SUPER_NR_INVALID_ARGUMENT = -1,
    SUPER_NR_NONFINITE = -2,
    SUPER_NR_FFT_ERROR = -3
} super_nr_status;

/* Forward: real[1024] -> packed[1024], unnormalized.
 * Inverse: packed[1024] -> real[1024], normalized by 1/1024.
 * packed[0]=DC, packed[1]=Nyquist, packed[2*k:2*k+2]=Re/Im(k).
 * Input and output never alias. Return zero on success. */
typedef int (*super_nr_fft_fn)(void *context, const float *input,
                                float *output, int inverse);

typedef struct {
    uint16_t lower[SUPER_NR_HOP];
    float lower_weight[SUPER_NR_HOP];
    float upper_weight[SUPER_NR_HOP];
    float window[SUPER_NR_FFT_SIZE];
    float previous[SUPER_NR_HOP];
    float tail[SUPER_NR_HOP];
    float frame[SUPER_NR_FFT_SIZE];
    float spectrum[SUPER_NR_FFT_SIZE];
    float power[SUPER_NR_POWERS];
    float mask[SUPER_NR_POWERS];
    float floor[SUPER_NR_POWERS];
    float noise[SUPER_NR_POWERS];
    float enhanced[SUPER_NR_POWERS];
    float gain[SUPER_NR_POWERS];
    float xi[SUPER_NR_POWERS];
    float nu[SUPER_NR_POWERS];
    float smoothed_xi[SUPER_NR_POWERS];
    float smoothed_power[SUPER_NR_HOP];
    float minimum[SUPER_NR_HOP];
    float alternate[SUPER_NR_HOP];
    uint32_t flags[SUPER_NR_HOP];
    uint32_t frames;
    uint32_t minimum_counter;
} super_nr_spectral_state;

typedef struct {
    float correlation[161];
    float history[320 + SUPER_NR_HOP];
    float input_power;
    float prediction_power;
    float last_prediction;
    float last_gain;
} super_nr_time_state;

typedef struct {
    float real[SUPER_NR_FFT_SIZE];
    fftwf_complex complex[SUPER_NR_HOP + 1];
    fftwf_plan forward;
    fftwf_plan inverse;
} super_nr_fft_workspace;

typedef struct {
    super_nr_spectral_state spectral;
    super_nr_time_state time;
    super_nr_fft_workspace fft;
    super_nr_fft_fn fft_function;
    void *fft_context;
    uint32_t initialized;
    float sample_rate;
} super_nr;

/* Audio integration contract:
 * - Each call takes exactly SUPER_NR_HOP (512) contiguous, writable samples.
 *   The layout is mono float[512], not interleaved stereo, complex IQ, or PCM
 *   bytes. float is assumed to be IEEE-754 binary32 in native memory layout.
 * - Blocks are consecutive samples of one stream. Reuse the same state across
 *   calls; initialize/reset only for an intentionally new stream.
 * - super_nr_init uses the recovered 40000 samples/second configuration.
 *   super_nr_init_rate adapts the band mapping to the receiver sample rate.
 *   FFT/hop sizes and sample-domain adaptation constants remain unchanged.
 * - Amplitudes are unnormalized, signed-16-bit-PCM-derived float units AFTER
 *   the receiver's demodulation/filtering/AGC. The original conversion casts
 *   int16_t directly to float, without division by 32768. The actual NR input
 *   amplitude therefore depends on the upstream AGC; it is not fixed full scale.
 * - For normalized [-1,1] PCM, an explicit multiply-by-32768 before NR and
 *   divide-by-32768 afterward gives PCM numeric units, but does NOT reproduce
 *   the receiver's demodulation or AGC. Passing normalized PCM directly changes
 *   the absolute-power constants (1 and 20) and thus the NR behavior.
 * - Output overwrites the input in the SAME amplitude units. There is no
 *   clipping, saturation, integer conversion, or automatic normalization.
 * - Enabled processing requires finite inputs. Check every returned status.
 *   The time-NR path uses delayed input while prediction power is zero.
 *   Never pass such output directly to playback or integer PCM conversion.
 * - Spectral synthesis has a 512-sample unity-mask delay. The time FIR is
 *   centered on a 160-sample-delayed input and has adaptive startup behavior;
 *   switching paths can therefore change latency/transients.
 *
 * One state per mono stream. No processing-time allocation.
 * NULL fft_function selects FFTW3 single-precision real FFT/IFFT.
 * Init creates plans; do not call init again without destroy.
 * States own plans bound to their buffers: do not copy or move live states.
 * Samples use firmware float amplitude units, NOT automatically normalized PCM.
 * See IMPLEMENTATION.txt for amplitude and state-lifecycle evidence. */
super_nr_status super_nr_init(super_nr *state,
                                 super_nr_fft_fn fft_function, void *fft_context);
super_nr_status super_nr_init_rate(super_nr *state, float sample_rate,
                                  super_nr_fft_fn fft_function, void *fft_context);
super_nr_status super_nr_reset(super_nr *state);
void super_nr_destroy(super_nr *state);

/* In-place, exactly 512 samples. enable==1 selects NR; other byte values bypass.
 * Bandwidth <1000 selects time NR, otherwise spectral NR.
 * Bypass and switching paths preserve the inactive path's state.
 * Invalid arguments/nonfinite inputs leave state and samples unchanged.
 * Disabled bypass leaves all inputs unchanged, including nonfinite inputs.
 * NONFINITE can report arithmetic overflow with partially advanced state.
 * FFT_ERROR can leave analysis state advanced; reset before retrying. */
super_nr_status super_nr_process(super_nr *state, float *samples,
                                    uint8_t enable, int32_t bandwidth_hz,
                                    uint8_t level);
super_nr_status super_nr_process_spectral(super_nr *state, float *samples,
                                             uint8_t level, int apply);
super_nr_status super_nr_process_time(super_nr *state, float *samples);

/* FFTW backend. Workspace must belong to an initialized default-backend state. */
int super_nr_fftw_fft(void *workspace, const float *input,
                       float *output, int inverse);

#ifdef __cplusplus
}
#endif
#endif
