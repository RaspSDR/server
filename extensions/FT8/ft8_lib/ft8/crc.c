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

void ftx_crc(uint8_t msg1[], int msglen, uint8_t out[14])
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

int
ftx_check_crc(const uint8_t a91[FTX_LDPC_K])
{
    uint8_t out1[14];

    // [1]: 'The CRC is calculated on the source-encoded message, zero-extended from 77 to 82 bits.'
    ftx_crc(a91, 82, out1);

    for (int i = 0; i < 14; i++)
    {
        if (out1[i] != a91[FTX_LDPC_K - FTX_CRC_WIDTH + i])
        {
            return 0;
        }
    }
    return 1;
}
