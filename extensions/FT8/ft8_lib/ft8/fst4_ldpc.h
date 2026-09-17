#ifndef _INCLUDE_FST4_LDPC_H_
#define _INCLUDE_FST4_LDPC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// Encode a 101-bit message into a 240-bit LDPC codeword using (240,101) code.
/// @param[in] message  array of 101 bits stored as 13 bytes (MSB first)
/// @param[out] codeword array of 240 bits stored as 30 bytes (MSB first)
void fst4_ldpc_encode(const uint8_t* message, uint8_t* codeword);

/// Encode a 74-bit message into a 240-bit LDPC codeword using (240,74) code.
/// @param[in] message  array of 74 bits stored as 10 bytes (MSB first)
/// @param[out] codeword array of 240 bits stored as 30 bytes (MSB first)
void fst4w_ldpc_encode(const uint8_t* message, uint8_t* codeword);

/// BP decode a 240-bit codeword using (240,101) LDPC code.
/// @param[in] codeword  240-element array of log-likelihoods (LLR: positive = more likely 0)
/// @param[in] max_iters maximum BP iterations
/// @param[out] plain    240-element array of decoded hard bits (0 or 1)
/// @param[out] ok       number of parity check errors (0 = success)
void fst4_ldpc_decode(const float codeword[], int max_iters, uint8_t plain[], int* ok);

/// BP decode a 240-bit codeword using (240,74) LDPC code.
/// @param[in] codeword  240-element array of log-likelihoods (LLR: positive = more likely 0)
/// @param[in] max_iters maximum BP iterations
/// @param[out] plain    240-element array of decoded hard bits (0 or 1)
/// @param[out] ok       number of parity check errors (0 = success)
void fst4w_ldpc_decode(const float codeword[], int max_iters, uint8_t plain[], int* ok);

#ifdef __cplusplus
}
#endif

#endif // _INCLUDE_FST4_LDPC_H_
