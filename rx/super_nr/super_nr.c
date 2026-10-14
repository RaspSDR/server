#include "super_nr.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define INITIALIZED UINT32_C(0x534e5231)
#define PI 3.14159265358979323846264338327950288

static const float gain_table[21] = {
    0.821569979f, 1.02016997f, 1.20460999f, 1.37533998f, 1.53363001f,
    1.68092f, 1.81865001f, 1.94810998f, 2.07037997f, 2.18637991f,
    2.29688001f, 2.40254998f, 2.50391006f, 2.60143995f, 2.69550991f,
    2.78646994f, 2.87457991f, 2.96015f, 3.04332995f, 3.12431002f,
    3.20325994f
};

static float minimum(float a, float b)
{
    return a < b ? a : b;
}

static int finite_array(const float *samples, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; ++i) {
        if (!isfinite(samples[i])) {
            return 0;
        }
    }
    return 1;
}

static int finite_samples(const float *samples)
{
    return finite_array(samples, SUPER_NR_HOP);
}

static super_nr_status check(const super_nr *state, const float *samples)
{
    if (state == NULL || samples == NULL || state->initialized != INITIALIZED) {
        return SUPER_NR_INVALID_ARGUMENT;
    }
    return finite_samples(samples) ? SUPER_NR_OK : SUPER_NR_NONFINITE;
}

static float band_coordinate(float frequency)
{
    float first = atanf(frequency * 0.00074f);
    float second = atanf((frequency * frequency) * 1.85e-8f) * 2.24f;
    return fmaf(frequency, 0.0001f, fmaf(first, 13.1f, second));
}

static void initialize_tables(super_nr *state)
{
    unsigned i;
    float spacing = band_coordinate(state->sample_rate * 0.5f) / 23.0f;
    for (i = 0; i < SUPER_NR_HOP; ++i) {
        float frequency = (float)i * (state->sample_rate / SUPER_NR_FFT_SIZE);
        float coordinate = band_coordinate(frequency);
        unsigned lower = (unsigned)floor((double)(coordinate / spacing));
        float upper_weight = fmaf(-spacing, (float)lower, coordinate) / spacing;
        state->spectral.lower[i] = (uint16_t)lower;
        state->spectral.upper_weight[i] = upper_weight;
        state->spectral.lower_weight[i] = 1.0f - upper_weight;
    }
    for (i = 0; i < SUPER_NR_FFT_SIZE; ++i) {
        float u = 4.0f * (float)i / SUPER_NR_FFT_SIZE;
        float distance = u < 1.0f ? u : u < 2.0f ? 2.0f - u :
                         u < 3.0f ? u - 2.0f : 4.0f - u;
        float argument = distance * 1.2719030380249023f;
        double taper = 0.5 - 0.5 * cos((double)argument * (PI / 2.0));
        float shaped = (float)(taper * taper);
        state->spectral.window[i] = sqrtf(
            u >= 1.0f && u < 3.0f ? 1.0f - shaped : shaped);
    }
}

static void reset_dsp(super_nr *state)
{
    unsigned i;
    memset(&state->spectral, 0, sizeof(state->spectral));
    memset(&state->time, 0, sizeof(state->time));
    initialize_tables(state);
    for (i = 0; i < SUPER_NR_POWERS; ++i) {
        state->spectral.noise[i] = 1.0f;
        state->spectral.enhanced[i] = 1.0f;
        state->spectral.gain[i] = 1.0f;
        state->spectral.xi[i] = 1.0f;
        state->spectral.nu[i] = 1.0f;
    }
    for (i = 0; i < SUPER_NR_HOP; ++i) {
        state->spectral.flags[i] = 1;
    }
}

super_nr_status super_nr_init(super_nr *state,
                                 super_nr_fft_fn fft_function, void *fft_context)
{
    return super_nr_init_rate(state, SUPER_NR_REFERENCE_RATE,
                              fft_function, fft_context);
}

super_nr_status super_nr_init_rate(super_nr *state, float sample_rate,
                                  super_nr_fft_fn fft_function, void *fft_context)
{
    if (state == NULL || !isfinite(sample_rate) ||
        sample_rate < 1000.0f || sample_rate > 192000.0f) {
        return SUPER_NR_INVALID_ARGUMENT;
    }
    memset(state, 0, sizeof(*state));
    state->sample_rate = sample_rate;
    state->fft_function = fft_function;
    state->fft_context = fft_context;
    if (fft_function == NULL) {
        state->fft.forward = fftwf_plan_dft_r2c_1d(
            SUPER_NR_FFT_SIZE, state->fft.real, state->fft.complex, FFTW_ESTIMATE);
        state->fft.inverse = fftwf_plan_dft_c2r_1d(
            SUPER_NR_FFT_SIZE, state->fft.complex, state->fft.real, FFTW_ESTIMATE);
        if (state->fft.forward == NULL || state->fft.inverse == NULL) {
            if (state->fft.forward != NULL) {
                fftwf_destroy_plan(state->fft.forward);
            }
            if (state->fft.inverse != NULL) {
                fftwf_destroy_plan(state->fft.inverse);
            }
            memset(state, 0, sizeof(*state));
            return SUPER_NR_FFT_ERROR;
        }
    }
    reset_dsp(state);
    state->initialized = INITIALIZED;
    return SUPER_NR_OK;
}

super_nr_status super_nr_reset(super_nr *state)
{
    if (state == NULL || state->initialized != INITIALIZED) {
        return SUPER_NR_INVALID_ARGUMENT;
    }
    reset_dsp(state);
    return SUPER_NR_OK;
}

void super_nr_destroy(super_nr *state)
{
    if (state == NULL || state->initialized != INITIALIZED) {
        return;
    }
    if (state->fft.forward != NULL) {
        fftwf_destroy_plan(state->fft.forward);
    }
    if (state->fft.inverse != NULL) {
        fftwf_destroy_plan(state->fft.inverse);
    }
    memset(state, 0, sizeof(*state));
}

int super_nr_fftw_fft(void *workspace, const float *input,
                       float *output, int inverse)
{
    super_nr_fft_workspace *work = (super_nr_fft_workspace *)workspace;
    unsigned i;
    if (work == NULL || input == NULL || output == NULL || input == output ||
        work->forward == NULL || work->inverse == NULL) {
        return -1;
    }
    if (inverse) {
        work->complex[0][0] = input[0];
        work->complex[SUPER_NR_HOP][0] = input[1];
        work->complex[0][1] = work->complex[SUPER_NR_HOP][1] = 0.0f;
        for (i = 1; i < SUPER_NR_HOP; ++i) {
            work->complex[i][0] = input[2 * i];
            work->complex[i][1] = input[2 * i + 1];
        }
        fftwf_execute(work->inverse);
    } else {
        memcpy(work->real, input, sizeof(work->real));
        fftwf_execute(work->forward);
    }
    if (inverse) {
        for (i = 0; i < SUPER_NR_FFT_SIZE; ++i) {
            output[i] = work->real[i] / SUPER_NR_FFT_SIZE;
        }
    } else {
        output[0] = work->complex[0][0];
        output[1] = work->complex[SUPER_NR_HOP][0];
        for (i = 1; i < SUPER_NR_HOP; ++i) {
            output[2 * i] = work->complex[i][0];
            output[2 * i + 1] = work->complex[i][1];
        }
    }
    return 0;
}

static int transform(super_nr *state, const float *input, float *output,
                     int inverse)
{
    if (state->fft_function != NULL) {
        return state->fft_function(state->fft_context, input, output, inverse);
    }
    return super_nr_fftw_fft(&state->fft, input, output, inverse);
}

static void map_bands(const super_nr_spectral_state *s, float *values)
{
    unsigned i;
    memset(values + SUPER_NR_HOP, 0, SUPER_NR_BANDS * sizeof(float));
    for (i = 0; i < SUPER_NR_HOP; ++i) {
        unsigned lower = SUPER_NR_HOP + s->lower[i];
        values[lower] = fmaf(values[i], s->lower_weight[i], values[lower]);
        values[lower + 1] = fmaf(values[i], s->upper_weight[i], values[lower + 1]);
    }
}

static void interpolate(const super_nr_spectral_state *s, float *values)
{
    unsigned i;
    for (i = 0; i < SUPER_NR_HOP; ++i) {
        unsigned lower = SUPER_NR_HOP + s->lower[i];
        float upper = s->upper_weight[i] * values[lower + 1];
        values[i] = fmaf(s->lower_weight[i], values[lower], upper);
    }
}

static float speech_gain(float xi, float nu, float *v)
{
    float ratio = xi / (xi + 1.0f);
    float twice, integral, factor;
    int index;
    *v = (nu + 1.0f) * ratio;
    twice = *v + *v;
    if (!isfinite(twice)) {
        return NAN;
    }
    integral = (float)floor((double)twice);
    /* Finite input states bound v to 101. Guard the conversion for corrupt
     * or overflowed state rather than invoking undefined float-to-int behavior. */
    if (integral > 19.0f) {
        factor = (float)(1.0 + 0.1296 / (double)*v);
    } else if (integral < 0.0f) {
        factor = 1.0f;
    } else {
        float fraction = twice - integral;
        float numerator;
        index = (int)integral;
        numerator = fmaf(1.0f - fraction, gain_table[index],
                         fraction * gain_table[index + 1]);
        factor = numerator / sqrtf(*v + 0.0001f);
    }
    return minimum(ratio * factor, 1.0f);
}

super_nr_status super_nr_process_spectral(super_nr *state, float *samples,
                                             uint8_t level, int apply)
{
    super_nr_spectral_state *s;
    super_nr_status status = check(state, samples);
    unsigned i, interval;
    uint32_t count;
    float alpha, old_weight, mean = 0.0f, factor, level_power;
    if (status != SUPER_NR_OK) {
        return status;
    }
    if (level > 30) {
        return SUPER_NR_INVALID_ARGUMENT;
    }
    s = &state->spectral;
    count = s->frames + 1;
    alpha = count <= 20000 ? 1.0f / (float)count : 0.03f;
    if ((double)alpha < 0.03) {
        alpha = 0.03f;
    }
    old_weight = count <= 33 ? 1.0f - alpha : 0.97f;
    s->frames = count > 20000 ? 20000 : count;
    ++s->minimum_counter;
    for (i = 0; i < SUPER_NR_HOP; ++i) {
        s->frame[i] = s->previous[i] * s->window[i];
        s->frame[i + SUPER_NR_HOP] = samples[i] * s->window[i + SUPER_NR_HOP];
    }
    memcpy(s->previous, samples, sizeof(s->previous));
    if (transform(state, s->frame, s->spectrum, 0) != 0) {
        return SUPER_NR_FFT_ERROR;
    }
    for (i = 0; i < SUPER_NR_HOP; ++i) {
        float real = s->spectrum[2 * i], imaginary = s->spectrum[2 * i + 1];
        s->power[i] = fmaf(imaginary, imaginary, real * real);
    }
    map_bands(s, s->power);
    if (!finite_array(s->power, SUPER_NR_POWERS)) {
        return SUPER_NR_NONFINITE;
    }
    for (i = 1; i < SUPER_NR_HOP - 1; ++i) {
        float smooth = s->power[i - 1] * 0.05f;
        smooth = fmaf(s->smoothed_power[i], 0.8f, smooth);
        smooth = fmaf(s->power[i], 0.1f, smooth);
        s->smoothed_power[i] = fmaf(s->power[i + 1], 0.05f, smooth);
    }
    s->smoothed_power[0] = fmaf(s->smoothed_power[0], 0.8f, 0.2f * s->power[0]);
    s->smoothed_power[511] = fmaf(s->smoothed_power[511], 0.8f, 0.2f * s->power[511]);
    if (s->frames == 1) {
        memset(s->minimum, 0, sizeof(s->minimum));
        memset(s->alternate, 0, sizeof(s->alternate));
    }
    interval = s->frames <= 9 ? 0 : s->frames <= 49 ? 5 :
               s->frames <= 99 ? 10 : s->frames < 1000 ? 15 :
               s->frames <= 9999 ? 20 : 25;
    if (s->minimum_counter > interval) {
        s->minimum_counter = 0;
        for (i = 0; i < SUPER_NR_HOP; ++i) {
            s->minimum[i] = minimum(s->smoothed_power[i], s->alternate[i]);
            s->alternate[i] = s->smoothed_power[i];
        }
    } else {
        for (i = 0; i < SUPER_NR_HOP; ++i) {
            s->minimum[i] = minimum(s->smoothed_power[i], s->minimum[i]);
            s->alternate[i] = minimum(s->smoothed_power[i], s->alternate[i]);
        }
    }
    for (i = 0; i < SUPER_NR_HOP; ++i) {
        s->flags[i] = 0.4f * s->smoothed_power[i] > s->minimum[i] + 20.0f;
        if (s->frames <= 9 && i > 0 && i < 511) {
            s->flags[i] = 0;
        }
        if (s->flags[i] == 0 || s->power[i] < s->noise[i]) {
            float noise = fmaf(old_weight, s->noise[i], alpha * s->power[i]);
            s->noise[i] = noise < 0.0f ? 0.0f : noise;
        }
    }
    map_bands(s, s->noise);
    if (!finite_array(s->noise, SUPER_NR_POWERS)) {
        return SUPER_NR_NONFINITE;
    }
    if (s->frames == 1) {
        memcpy(s->enhanced, s->power, sizeof(s->enhanced));
    }
    for (i = 0; i < SUPER_NR_POWERS; ++i) {
        float denominator = s->noise[i] + 1.0f;
        float excess = minimum(s->power[i] / denominator - 1.0f, 100.0f);
        float ratio = s->enhanced[i] / (denominator + s->enhanced[i]);
        float weight = fmaf(ratio * ratio, 0.89f, 0.1f);
        float estimate = (1.0f - weight) * (s->enhanced[i] / denominator);
        s->nu[i] = excess;
        if (excess < 0.0f) {
            excess = 0.0f;
        }
        s->xi[i] = minimum(fmaf(excess, weight, estimate), 100.0f);
    }
    if (!finite_array(s->xi, SUPER_NR_POWERS) ||
        !finite_array(s->nu, SUPER_NR_POWERS)) {
        return SUPER_NR_NONFINITE;
    }
    s->smoothed_xi[0] = fmaf(s->smoothed_xi[0], 0.7f, 0.3f * s->xi[0]);
    for (i = 1; i < 511; ++i) {
        float value = 0.15f * s->xi[i];
        value = fmaf(s->smoothed_xi[i], 0.7f, value);
        value = fmaf(s->xi[i - 1], 0.075f, value);
        s->smoothed_xi[i] = fmaf(s->xi[i + 1], 0.075f, value);
    }
    for (i = 511; i < SUPER_NR_POWERS; ++i) {
        s->smoothed_xi[i] = fmaf(s->smoothed_xi[i], 0.7f, 0.3f * s->xi[i]);
    }
    for (i = SUPER_NR_HOP; i < SUPER_NR_POWERS; ++i) {
        mean += s->smoothed_xi[i];
    }
    mean /= SUPER_NR_BANDS;
    factor = fmaf(1.0f / (1.0f + 0.15f / mean), 0.899f, 0.1f);
    level_power = expf(-(float)level * 0.2302584946155548f);
    for (i = SUPER_NR_HOP; i < SUPER_NR_POWERS; ++i) {
        float v, gain = speech_gain(s->xi[i], s->nu[i], &v);
        float absence, odds, probability_denominator;
        s->floor[i] = sqrtf(level_power * s->noise[i]) / sqrtf(s->noise[i] + 1.0f);
        s->gain[i] = gain;
        s->enhanced[i] = fmaf(s->enhanced[i], 0.2f, s->power[i] * (gain * gain * 0.8f));
        absence = fmaf(-factor,
                      fmaf(1.0f / (1.0f + 0.15f / s->smoothed_xi[i]), 0.8f, 0.199f),
                      1.0f);
        odds = (absence / (1.0f - absence)) * (s->xi[i] + 1.0f);
        probability_denominator = fmaf(odds, expf(-v), 1.0f);
        s->mask[i] = 1.0f / probability_denominator;
    }
    interpolate(s, s->mask);
    interpolate(s, s->gain);
    interpolate(s, s->floor);
    for (i = 0; i < SUPER_NR_HOP; ++i) {
        float v, gain = speech_gain(s->xi[i], s->nu[i], &v);
        float probability = s->mask[i], mixed;
        if (s->gain[i] < gain * 0.333f) {
            gain = s->gain[i] * 3.0f;
        }
        s->enhanced[i] = fmaf(s->enhanced[i], 0.2f, (gain * gain * 0.8f) * s->power[i]);
        if (gain < s->floor[i]) {
            gain = s->floor[i];
        }
        s->gain[i] = gain;
        mixed = fmaf(1.0f - probability, sqrtf(s->floor[i]), probability * sqrtf(gain));
        s->mask[i] = mixed * mixed;
    }
    if (!finite_array(s->mask, SUPER_NR_POWERS) ||
        !finite_array(s->enhanced, SUPER_NR_POWERS)) {
        return SUPER_NR_NONFINITE;
    }
    if (apply != 1) {
        for (i = 0; i < SUPER_NR_POWERS; ++i) {
            s->mask[i] = 1.0f;
        }
    }
    for (i = 0; i < SUPER_NR_HOP; ++i) {
        s->spectrum[2 * i] *= s->mask[i];
        s->spectrum[2 * i + 1] *= s->mask[i];
    }
    if (transform(state, s->spectrum, s->frame, 1) != 0) {
        return SUPER_NR_FFT_ERROR;
    }
    for (i = 0; i < SUPER_NR_FFT_SIZE; ++i) {
        s->frame[i] *= s->window[i];
    }
    for (i = 0; i < SUPER_NR_HOP; ++i) {
        samples[i] = s->tail[i] + s->frame[i];
        s->tail[i] = s->frame[i + SUPER_NR_HOP];
    }
    return finite_samples(samples) ? SUPER_NR_OK : SUPER_NR_NONFINITE;
}

super_nr_status super_nr_process_time(super_nr *state, float *samples)
{
    super_nr_time_state *s;
    super_nr_status status = check(state, samples);
    unsigned n, j;
    const float decay = 0.9995999932289124f, update = 0.0004000067710876465f;
    if (status != SUPER_NR_OK) {
        return status;
    }
    s = &state->time;
    memcpy(s->history + 320, samples, SUPER_NR_HOP * sizeof(float));
    for (n = 0; n < SUPER_NR_HOP; ++n) {
        float sample = samples[n], scaled = sample * update, prediction = 0.0f;
        for (j = 0; j <= 160; ++j) {
            s->correlation[j] = fmaf(s->correlation[j], decay,
                                     s->history[n + 160 + j] * scaled);
        }
        s->input_power = fmaf(s->input_power, decay, sample * scaled);
        for (j = 0; j < 321; ++j) {
            unsigned tap = j <= 160 ? j : 320 - j;
            prediction = fmaf(s->history[n + j], s->correlation[tap], prediction);
        }
        s->prediction_power = fmaf(s->prediction_power, decay,
                                    (prediction * update) * prediction);
        s->last_prediction = prediction;
        /* Prediction has no energy during silence and early FIR startup. */
        s->last_gain = s->prediction_power > 0.0f ?
            sqrtf(s->input_power / s->prediction_power) : 0.0f;
        samples[n] = s->prediction_power > 0.0f ?
            s->last_gain * prediction : s->history[n + 160];
    }
    memmove(s->history, s->history + SUPER_NR_HOP, 320 * sizeof(float));
    return finite_samples(samples) ? SUPER_NR_OK : SUPER_NR_NONFINITE;
}

super_nr_status super_nr_process(super_nr *state, float *samples,
                                    uint8_t enable, int32_t bandwidth_hz,
                                    uint8_t level)
{
    super_nr_status status;
    if (state == NULL || samples == NULL || state->initialized != INITIALIZED) {
        return SUPER_NR_INVALID_ARGUMENT;
    }
    if (enable != 1) {
        return SUPER_NR_OK;
    }
    status = check(state, samples);
    if (status != SUPER_NR_OK) {
        return status;
    }
    if (bandwidth_hz < 1000) {
        return super_nr_process_time(state, samples);
    }
    return super_nr_process_spectral(state, samples, level, 1);
}
