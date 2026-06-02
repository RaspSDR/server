#include "crc.h"
#include "constants.h"

#define TOPBIT (1u << (FTX_CRC_WIDTH - 1))

// Compute 14-bit CRC for a sequence of given number of bits
// Adapted from https://barrgroup.com/Embedded-Systems/How-To/CRC-Calculation-C-Code
// [IN] message  - byte sequence (MSB first)
// [IN] num_bits - number of bits in the sequence
uint16_t ftx_compute_crc(const uint8_t message[], int num_bits)
{
    uint16_t remainder = 0;
    int idx_byte = 0;

    // Perform modulo-2 division, a bit at a time.
    for (int idx_bit = 0; idx_bit < num_bits; ++idx_bit)
    {
        if (idx_bit % 8 == 0)
        {
            // Bring the next byte into the remainder.
            remainder ^= (message[idx_byte] << (FTX_CRC_WIDTH - 8));
            ++idx_byte;
        }

        // Try to divide the current data bit.
        if (remainder & TOPBIT)
        {
            remainder = (remainder << 1) ^ FTX_CRC_POLYNOMIAL;
        }
        else
        {
            remainder = (remainder << 1);
        }
    }

    return remainder & ((TOPBIT << 1) - 1u);
}

#define FST4_TOPBIT (1u << (FST4_CRC_WIDTH - 1))

/// Compute CRC-24 matching WSJT-X get_crc24.f90 algorithm exactly.
/// Processes individual bits from packed bytes (MSB-first byte order).
/// The Fortran code works on individual bit arrays; this version unpacks from bytes.
uint32_t fst4_compute_crc(const uint8_t message[], int num_bits)
{
    // Extract individual bits from packed bytes (MSB first)
    // Equivalent to Fortran mc(1:num_bits)
    
    // Polynomial as bit array: p(1:25) = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,0,1,1,0,1,1}
    // This is 0x100065B
    static const uint8_t poly[25] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,0,1,1,0,1,1};
    
    // Helper to get bit i from packed bytes (MSB-first)
    #define GET_BIT(arr, i) (((arr)[(i)/8] >> (7 - ((i) % 8))) & 1)
    
    // Initialize shift register with first 25 message bits
    uint8_t r[25];
    for (int i = 0; i < 25 && i < num_bits; i++)
    {
        r[i] = GET_BIT(message, i);
    }
    
    // Process remaining bits
    for (int i = 0; i <= num_bits - 25; i++)
    {
        if (i + 24 < num_bits)
            r[24] = GET_BIT(message, i + 24);
        else
            r[24] = 0;
        
        uint8_t feedback = r[0];
        // XOR with polynomial if MSB is 1
        if (feedback)
        {
            for (int j = 0; j < 25; j++)
                r[j] ^= poly[j];
        }
        // Circular shift left by 1
        uint8_t tmp = r[0];
        for (int j = 0; j < 24; j++)
            r[j] = r[j + 1];
        r[24] = tmp;
    }
    
    // Convert r[0:23] to 24-bit integer
    uint32_t crc = 0;
    for (int i = 0; i < 24; i++)
    {
        crc = (crc << 1) | r[i];
    }
    
    #undef GET_BIT
    return crc;
}

uint16_t ftx_extract_crc(const uint8_t a91[])
{
    uint16_t chksum = ((a91[FTX_LDPC_K_BYTES - 3] & 0x07) << 11) | (a91[FTX_LDPC_K_BYTES - 2] << 3) | (a91[FTX_LDPC_K_BYTES - 1] >> 5);
    return chksum;
}

void ftx_add_crc(const uint8_t payload[], uint8_t a91[])
{
    // Copy 77 bits of payload data
    for (int i = 0; i < 10; i++)
        a91[i] = payload[i];

    // Clear 3 bits after the payload to make 82 bits
    a91[9] &= 0xF8u;
    a91[10] = 0;

    // Calculate CRC of 82 bits (77 + 5 zeros)
    // 'The CRC is calculated on the source-encoded message, zero-extended from 77 to 82 bits'
    uint16_t checksum = ftx_compute_crc(a91, 96 - 14);

    // Store the CRC at the end of 77 bit message
    a91[9] |= (uint8_t)(checksum >> 11);
    a91[10] = (uint8_t)(checksum >> 3);
    a91[11] = (uint8_t)(checksum << 5);
}

void ftx_crc(const uint8_t msg1[], int msglen, uint8_t out[14])
{
    // the old FT8 polynomial for 12-bit CRC, 0xc06.
    // int div[] = { 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0 };

    // the new FT8 polynomial for 14-bit CRC, 0x2757,
    // with leading 1 bit.
    uint8_t div[] = { 1, 1, 0, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1 };

    // append 14 zeros.
    uint8_t msg[FTX_LDPC_M + FTX_CRC_WIDTH];
    for (int i = 0; i < msglen + FTX_CRC_WIDTH; i++)
    {
        if (i < 77)
        {
            msg[i] = msg1[i];
        }
        else
        {
            msg[i] = 0;
        }
    }

    for (int i = 0; i < msglen; i++)
    {
        if (msg[i])
        {
            for (int j = 0; j < FTX_CRC_WIDTH + 1; j++)
            {
                msg[i + j] = msg[i + j] ^ div[j];
            }
        }
    }

    for (int i = 0; i < FTX_CRC_WIDTH; i++)
    {
        out[i] = msg[msglen + i];
    }
}

bool
ftx_check_crc(const uint8_t a91[FTX_LDPC_K])
{
    uint8_t out1[14];

    // [1]: 'The CRC is calculated on the source-encoded message, zero-extended from 77 to 82 bits.'
    ftx_crc(a91, 82, out1);

    for (int i = 0; i < 14; i++)
    {
        if (out1[i] != a91[FTX_LDPC_K - FTX_CRC_WIDTH + i])
        {
            return false;
        }
    }
    return true;
}

bool ftx_check_crc_packed(const uint8_t a91_packed[])
{
    uint16_t crc_stored = ftx_extract_crc(a91_packed);

    // Compute CRC over 82 bits (77 payload + 5 zeros)
    uint8_t tmp[FTX_LDPC_K_BYTES];
    for (int i = 0; i < 10; i++)
        tmp[i] = a91_packed[i];
    tmp[9] &= 0xF8u;
    tmp[10] = 0;
    tmp[11] = 0;

    uint16_t crc_computed = ftx_compute_crc(tmp, 82);
    return crc_stored == crc_computed;
}

uint32_t fst4_extract_crc(const uint8_t a101[])
{
    // CRC-24 starts at bit 77 in the 101-bit message
    uint32_t chksum = ((uint32_t)(a101[9] & 0x07) << 21) |
                      ((uint32_t)a101[10] << 13) |
                      ((uint32_t)a101[11] << 5) |
                      ((uint32_t)a101[12] >> 3);
    return chksum;
}

void fst4_add_crc(const uint8_t payload[], uint8_t a101[])
{
    // Copy 77 bits of payload
    for (int i = 0; i < 10; i++)
        a101[i] = payload[i];

    // Clear bits after payload, zero-extend to 101 bits (13 bytes)
    a101[9] &= 0xF8u; // Keep top 5 bits of byte 9 (bits 72-76)
    a101[10] = 0;
    a101[11] = 0;
    a101[12] = 0;

    // CRC is computed over 77 payload bits + 24 zeros = 101 bits
    // (from get_crc24.f90: ncrc=24, the CRC covers payload + zero padding)
    uint32_t checksum = fst4_compute_crc(a101, 77 + 24);

    // Store CRC-24 starting at bit 77
    a101[9] |= (uint8_t)(checksum >> 21);
    a101[10] = (uint8_t)(checksum >> 13);
    a101[11] = (uint8_t)(checksum >> 5);
    a101[12] = (uint8_t)(checksum << 3);
}

bool fst4_check_crc(const uint8_t a101[])
{
    // Extract stored CRC
    uint32_t crc_stored = fst4_extract_crc(a101);

    // Compute CRC over first 77 bits + 24 zero bits
    uint8_t tmp[13];
    for (int i = 0; i < 10; i++)
        tmp[i] = a101[i];
    tmp[9] &= 0xF8u;
    tmp[10] = 0;
    tmp[11] = 0;
    tmp[12] = 0;

    uint32_t crc_computed = fst4_compute_crc(tmp, 77 + 24);
    return crc_stored == crc_computed;
}

uint32_t fst4w_extract_crc(const uint8_t a74[])
{
    // CRC-24 starts at bit 50 in the 74-bit message
    uint32_t chksum = ((uint32_t)(a74[6] & 0x3F) << 18) |
                      ((uint32_t)a74[7] << 10) |
                      ((uint32_t)a74[8] << 2) |
                      ((uint32_t)a74[9] >> 6);
    return chksum;
}

void fst4w_add_crc(const uint8_t payload[], uint8_t a74[])
{
    // Copy 50 bits of payload (7 bytes, top 2 bits of byte 6 are payload)
    for (int i = 0; i < 7; i++)
        a74[i] = payload[i];

    // Clear bits after payload, zero-extend to 74 bits (10 bytes)
    a74[6] &= 0xC0u; // Keep top 2 bits of byte 6 (bits 48-49)
    a74[7] = 0;
    a74[8] = 0;
    a74[9] = 0;

    // CRC is computed over 50 payload bits + 24 zeros = 74 bits
    uint32_t checksum = fst4_compute_crc(a74, 50 + 24);

    // Store CRC-24 starting at bit 50
    a74[6] |= (uint8_t)(checksum >> 18);
    a74[7] = (uint8_t)(checksum >> 10);
    a74[8] = (uint8_t)(checksum >> 2);
    a74[9] = (uint8_t)(checksum << 6);
}

bool fst4w_check_crc(const uint8_t a74[])
{
    uint32_t crc_stored = fst4w_extract_crc(a74);

    uint8_t tmp[10];
    for (int i = 0; i < 7; i++)
        tmp[i] = a74[i];
    tmp[6] &= 0xC0u;
    tmp[7] = 0;
    tmp[8] = 0;
    tmp[9] = 0;

    uint32_t crc_computed = fst4_compute_crc(tmp, 50 + 24);
    return crc_stored == crc_computed;
}
