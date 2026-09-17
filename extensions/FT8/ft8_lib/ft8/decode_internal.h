#ifndef _INCLUDE_DECODE_INTERNAL_H_
#define _INCLUDE_DECODE_INTERNAL_H_

/// Internal header for protocol-specific decode implementations.
/// Not part of the public API — only used by ft8_decode.c, ft4_decode.c, fst4_decode.c.

#include "decode.h"
#include "constants.h"

#include <math.h>

/// Get pointer to the first magnitude bin for a candidate's first symbol
static inline const WF_ELEM_T* get_cand_mag(const ftx_waterfall_t* wf, const ftx_candidate_t* candidate)
{
    int offset = candidate->time_offset;
    offset = (offset * wf->time_osr) + candidate->time_sub;
    offset = (offset * wf->freq_osr) + candidate->freq_sub;
    offset = (offset * wf->num_bins) + candidate->freq_offset;
    return wf->mag + offset;
}

static inline float max2(float a, float b)
{
    return (a >= b) ? a : b;
}

static inline float max4(float a, float b, float c, float d)
{
    return max2(max2(a, b), max2(c, d));
}

// Lookup table for y = 10*log10(1 + 10^(x/10)), where
//   y - increase in signal level dB when adding a weaker independent signal
//   x - specific relative strength of the weaker signal in dB
// Table index corresponds to x in dB (index 0: 0 dB, index 1: -1 dB etc)
// Used by ft8_decode_multi_symbols
static const float db_power_sum[40] = {
    3.01029995663981f, 2.53901891043867f, 2.1244260279434f, 1.76434862436485f, 1.45540463109294f,
    1.19331048066095f, 0.973227937086954f, 0.790097496525665f, 0.638920341433796f, 0.514969420252302f,
    0.413926851582251f, 0.331956199884278f, 0.265723755961025f, 0.212384019142551f, 0.16954289279533f,
    0.135209221080382f, 0.10774225511957f, 0.085799992300358f, 0.06829128312453f, 0.054333142200458f,
    0.043213737826426f, 0.034360947517284f, 0.027316043349389f, 0.021711921641451f, 0.017255250287928f,
    0.013711928326833f, 0.010895305999614f, 0.008656680827934f, 0.006877654943187f, 0.005464004928574f,
    0.004340774793186f, 0.003448354310253f, 0.002739348814965f, 0.002176083232619f, 0.001728613409904f,
    0.001373142636584f, 0.001090761428665f, 0.000866444976964f, 0.000688255828734f, 0.000546709946839f
};

/// Compute unnormalized log likelihood of 2 message bits (1 FSK symbol, 4 tones)
/// Shared by FT4 and FST4
void ft4_extract_symbol(const WF_ELEM_T* wf, float* logl);

#endif // _INCLUDE_DECODE_INTERNAL_H_
