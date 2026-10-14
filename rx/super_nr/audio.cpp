#include "audio.h"

#include <math.h>

super_nr_status SuperNR::init(float sample_rate)
{
    if (state.initialized)
        return SUPER_NR_OK;
    return super_nr_init_rate(&state, sample_rate, NULL, NULL);
}

super_nr_status SuperNR::reset()
{
    return state.initialized ? super_nr_reset(&state) : SUPER_NR_OK;
}

void SuperNR::destroy()
{
    super_nr_destroy(&state);
}

super_nr_status SuperNR::process(int count, int16_t *pcm, int bandwidth, int level,
                                float output_gain)
{
    if (!pcm || count != SUPER_NR_HOP || level < 0 || level > 30 ||
        !isfinite(output_gain) || output_gain <= 0.0f || output_gain > 32.0f)
        return SUPER_NR_INVALID_ARGUMENT;
    for (int i = 0; i < count; i++)
        samples[i] = pcm[i];
    super_nr_status status = super_nr_process(&state, samples, 1, bandwidth, level);
    if (status != SUPER_NR_OK)
        return status;
    for (int i = 0; i < count; i++) {
        float sample = samples[i] * output_gain;
        pcm[i] = sample >= 32767.0f ? 32767 :
            sample <= -32768.0f ? -32768 : (int16_t) sample;
    }
    return SUPER_NR_OK;
}
