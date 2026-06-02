#ifndef _INCLUDE_CRC_H_
#define _INCLUDE_CRC_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Compute 14-bit CRC for a sequence of given number of bits using FT8/FT4 CRC polynomial
// [IN] message  - byte sequence (MSB first)
// [IN] num_bits - number of bits in the sequence
uint16_t ftx_compute_crc(const uint8_t message[], int num_bits);

// Compute 24-bit CRC for FST4/FST4W using polynomial 0x100065B
// [IN] message  - byte sequence (MSB first)
// [IN] num_bits - number of bits in the sequence
uint32_t fst4_compute_crc(const uint8_t message[], int num_bits);

/// Extract the FT8/FT4 CRC of a packed message (during decoding)
/// @param[in] a91 77 bits of payload data + CRC
/// @return Extracted CRC
uint16_t ftx_extract_crc(const uint8_t a91[]);

/// Add FT8/FT4 CRC to a packed message (during encoding)
/// @param[in] payload 77 bits of payload data
/// @param[out] a91 91 bits of payload data + CRC
void ftx_add_crc(const uint8_t payload[], uint8_t a91[]);

/// Extract FST4 CRC-24 from a 101-bit packed message (77 payload + 24 CRC)
/// @param[in] a101 packed message bits (MSB first, 13 bytes)
/// @return Extracted 24-bit CRC
uint32_t fst4_extract_crc(const uint8_t a101[]);

/// Add FST4 CRC-24 to a 77-bit payload, producing 101-bit message
/// @param[in] payload 77 bits of payload data (10 bytes, MSB first)
/// @param[out] a101 101 bits of payload + CRC (13 bytes)
void fst4_add_crc(const uint8_t payload[], uint8_t a101[]);

/// Check FST4 CRC-24 of a 101-bit packed message
/// @param[in] a101 packed message bits (101 bits in 13 bytes)
/// @return true if CRC is valid
bool fst4_check_crc(const uint8_t a101[]);

/// Extract FST4W CRC-24 from a 74-bit packed message (50 payload + 24 CRC)
/// @param[in] a74 packed message bits (MSB first, 10 bytes)
/// @return Extracted 24-bit CRC
uint32_t fst4w_extract_crc(const uint8_t a74[]);

/// Add FST4W CRC-24 to a 50-bit payload, producing 74-bit message
/// @param[in] payload 50 bits of payload data (7 bytes, MSB first)
/// @param[out] a74 74 bits of payload + CRC (10 bytes)
void fst4w_add_crc(const uint8_t payload[], uint8_t a74[]);

/// Check FST4W CRC-24 of a 74-bit packed message
/// @param[in] a74 packed message bits (74 bits in 10 bytes)
/// @return true if CRC is valid
bool fst4w_check_crc(const uint8_t a74[]);

// Calculate 14-bit CRC for a sequence of given number of bits
void ftx_crc(const uint8_t msg1[], int msglen, uint8_t out[]);

// check if packed bytes are crc valid (works on packed byte arrays like FST4 CRC functions)
bool ftx_check_crc_packed(const uint8_t a91_packed[]);

// check if bits are crc valid (legacy interface, works on unpacked bit arrays)
bool ftx_check_crc(const uint8_t a91[]);

#ifdef __cplusplus
}
#endif

#endif // _INCLUDE_CRC_H_