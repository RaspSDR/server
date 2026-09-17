//
// Shared fast math approximations for LDPC decoding.
//
// Provides fast_tanh / fast_atanh (scalar) and
// fast_tanh4 / fast_atanh4 (batch of 4, NEON-optimized on ARM).
//
// All functions are static inline for zero call overhead.
//

#ifndef _INCLUDE_FAST_MATH_H_
#define _INCLUDE_FAST_MATH_H_

#include <stdint.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define USE_NEON 1
#endif

// Rational polynomial approximation of tanh(x).
// Accurate to ~1e-4 for |x| < 4.97. Returns exact ±1.0 for |x| >= 4.97.
static inline float fast_tanh(float x)
{
    if (x < -4.97f)
        return -1.0f;
    if (x > 4.97f)
        return 1.0f;
    float x2 = x * x;
    float a = x * (945.0f + x2 * (105.0f + x2));
    float b = 945.0f + x2 * (420.0f + x2 * 15.0f);
    return a / b;
}

// Rational polynomial approximation of atanh(x).
// Valid for |x| < 1.
static inline float fast_atanh(float x)
{
    float x2 = x * x;
    float a = x * (945.0f + x2 * (-735.0f + x2 * 64.0f));
    float b = (945.0f + x2 * (-1050.0f + x2 * 225.0f));
    return a / b;
}

// Batch tanh: compute fast_tanh for 4 values at once.
// out[0..3] = fast_tanh(in[0..3])
static inline void fast_tanh4(float out[4], const float in[4])
{
#ifdef USE_NEON
    float32x4_t x = vld1q_f32(in);
    float32x4_t x2 = vmulq_f32(x, x);

    // Numerator: x * (945 + x2 * (105 + x2))
    float32x4_t a = vaddq_f32(vdupq_n_f32(105.0f), x2);
    a = vmlaq_f32(vdupq_n_f32(945.0f), x2, a);
    a = vmulq_f32(x, a);

    // Denominator: 945 + x2 * (420 + x2 * 15)
    float32x4_t b = vmlaq_f32(vdupq_n_f32(420.0f), x2, vdupq_n_f32(15.0f));
    b = vmlaq_f32(vdupq_n_f32(945.0f), x2, b);

    // Polynomial ratio
    float32x4_t result = vdivq_f32(a, b);

    // Clamp output to [-1, 1] — the polynomial overshoots slightly near ±4.97
    result = vminq_f32(result, vdupq_n_f32(1.0f));
    result = vmaxq_f32(result, vdupq_n_f32(-1.0f));

    vst1q_f32(out, result);
#else
    out[0] = fast_tanh(in[0]);
    out[1] = fast_tanh(in[1]);
    out[2] = fast_tanh(in[2]);
    out[3] = fast_tanh(in[3]);
#endif
}

// Batch atanh: compute fast_atanh for 4 values at once.
// out[0..3] = fast_atanh(in[0..3])
static inline void fast_atanh4(float out[4], const float in[4])
{
#ifdef USE_NEON
    float32x4_t x = vld1q_f32(in);
    float32x4_t x2 = vmulq_f32(x, x);

    // Numerator: x * (945 + x2 * (-735 + x2 * 64))
    float32x4_t a = vmlaq_f32(vdupq_n_f32(-735.0f), x2, vdupq_n_f32(64.0f));
    a = vmlaq_f32(vdupq_n_f32(945.0f), x2, a);
    a = vmulq_f32(x, a);

    // Denominator: 945 + x2 * (-1050 + x2 * 225)
    float32x4_t b = vmlaq_f32(vdupq_n_f32(-1050.0f), x2, vdupq_n_f32(225.0f));
    b = vmlaq_f32(vdupq_n_f32(945.0f), x2, b);

    float32x4_t result = vdivq_f32(a, b);
    vst1q_f32(out, result);
#else
    out[0] = fast_atanh(in[0]);
    out[1] = fast_atanh(in[1]);
    out[2] = fast_atanh(in[2]);
    out[3] = fast_atanh(in[3]);
#endif
}

#endif // _INCLUDE_FAST_MATH_H_
