#include "constants.h"
#include "osd.h"
#include "ldpc.h"

#include <math.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/*
 * Performs Gauss-Jordan elimination on a binary matrix to find its inverse.
 *
 * The function manipulates the input matrix `m` of dimensions
 * [FTX_LDPC_N][2*FTX_LDPC_K] (174x182) to compute the inverse of its left half,
 * storing the result in its upper-right quarter. The `rows` parameter is the
 * number of rows (91), and `cols` is the number of columns (174).
 *
 * The `which` array keeps track of column swaps to ensure the correct inverse is
 * found. The initial right half of `m` should be zeros, and the function
 * constructs an identity matrix in the upper-right quarter during execution.
 *
 * Returns 1 if the matrix is successfully inverted, or 0 if it is singular.
 */

int gauss_jordan(int rows, int cols, uint8_t m[FTX_LDPC_N][2 * FTX_LDPC_K], uint8_t which[FTX_LDPC_K])
{
    assert(rows == FTX_LDPC_K);
    assert(cols == FTX_LDPC_N);

    for (int row = 0; row < rows; row++)
    {
        if (m[row][row] != 1)
        {
            for (int row1 = row + 1; row1 < cols; row1++)
            {
                if (m[row1][row] == 1)
                {
                    // swap m[row] and m[row1]
                    for (int col = 0; col < 2 * rows; col++)
                    {
                        int tmp = m[row][col];
                        m[row][col] = m[row1][col];
                        m[row1][col] = tmp;
                    }
                    int tmp = which[row];
                    which[row] = which[row1];
                    which[row1] = tmp;
                    break;
                }
            }
        }
        if (m[row][row] != 1)
        {
            // could not invert
            return 0;
        }
        // lazy creation of identity matrix in the upper-right quarter
        m[row][rows + row] = (m[row][rows + row] + 1) % 2;
        // now eliminate
        for (int row1 = 0; row1 < cols; row1++)
        {
            if (row1 == row)
                continue;
            if (m[row1][row] == 0)
                continue;

            for (int col = 0; col < 2 * rows; col++)
            {
                m[row1][col] = (m[row1][col] + m[row][col]) % 2;
            }
        }
    }

    return 1;
}

/**
 * Score how well a given possible original codeword matches what was received.
 *
 * @param xplain Possible original codeword (91 bits).
 * @param ll174 Received codeword (174 bits) with log-likelihoods.
 * @return A score, where higher means better match.
 */
static float
osd_score(uint8_t xplain[FTX_LDPC_K], float ll174[FTX_LDPC_N])
{
    uint8_t xcode[FTX_LDPC_N];
    ldpc_encode(xplain, xcode);

    float score = 0;
    for (int i = 0; i < FTX_LDPC_N; i++)
    {
        if (xcode[i])
        {
            // one-bit, expect ll to be negative.
            score -= ll174[i] * 4.6f;
        }
        else
        {
            // zero-bit, expect ll to be positive.
            score += ll174[i] * 4.6f;
        }
    }

    return score;
}

void ft8_crc(uint8_t msg1[], int msglen, uint8_t out[14])
{
    // the old FT8 polynomial for 12-bit CRC, 0xc06.
    // int div[] = { 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0 };

    // the new FT8 polynomial for 14-bit CRC, 0x2757,
    // with leading 1 bit.
    uint8_t div[] = { 1, 1, 0, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1 };

    // append 14 zeros.
    uint8_t msg[128];
    for (int i = 0; i < msglen + 14; i++)
    {
        if (i < msglen)
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
            for (int j = 0; j < 15; j++)
            {
                msg[i + j] = (msg[i + j] + div[j]) % 2;
            }
        }
    }

    for (int i = 0; i < 14; i++)
    {
        out[i] = msg[msglen + i];
    }
}

static int
check_crc(const uint8_t a91[91])
{
    uint8_t aa[91];
    for (int i = 0; i < 91; i++)
    {
        if (i < 77)
        {
            aa[i] = a91[i];
        }
        else
        {
            aa[i] = 0;
        }
    }
    uint8_t out1[14];

    // why 82? why not 77?
    ft8_crc(aa, 82, out1);

    for (int i = 0; i < 14; i++)
    {
        if (out1[i] != a91[91 - 14 + i])
        {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Checks the plausibility of a decoded message.
 *
 * This function verifies whether a given decoded message, represented by
 * the 91-bit array `plain`, is plausible. It first checks if the message
 * is not all zeros. Then, it checks the message's CRC for validity.
 *
 * @param[in] plain The 91-bit array representing the decoded message.
 *
 * @return Returns 1 if the message is plausible (non-zero and valid CRC),
 *         otherwise returns 0.
 */

static int
osd_check(const uint8_t plain[FTX_LDPC_K])
{
    // does a decode look plausible?
    int allzero = 1;
    for (int i = 0; i < FTX_LDPC_K; i++)
    {
        if (plain[i] != 0)
        {
            allzero = 0;
        }
    }
    if (allzero)
    {
        return 0;
    }

    if (check_crc(plain) == 0)
    {
        return 0;
    }

    return 1;
}

/**
 * @brief Performs matrix multiplication and modulo operation.
 *
 * This function multiplies a matrix 'a' with a vector 'b' and stores the result in vector 'c'.
 * The multiplication is followed by a modulo 2 operation on each resulting element.
 *
 * @param[in] a A 2D array representing the matrix with dimensions [FTX_LDPC_K][FTX_LDPC_K].
 * @param[in] b A 1D array representing the vector with dimension [FTX_LDPC_K].
 * @param[out] c A 1D array representing the resulting vector after matrix multiplication and modulo operation.
 */
static void
matmul(uint8_t a[FTX_LDPC_K][FTX_LDPC_K], uint8_t b[FTX_LDPC_K], uint8_t c[FTX_LDPC_K])
{
    for (int i = 0; i < FTX_LDPC_K; i++)
    {
        int sum = 0;
        for (int j = 0; j < FTX_LDPC_K; j++)
        {
            sum += a[i][j] & b[j]; // one bit multiply
        }
        c[i] = sum % 2;
    }
}

/**
 * @brief qsort comparison function for sorting codeword indices.
 *
 * Compares two indices 'a' and 'b' based on the absolute value of the elements in the
 * 'codeword' array at positions 'a' and 'b'. The comparison is in descending order.
 *
 * @param[in] a A pointer to the first index.
 * @param[in] b A pointer to the second index.
 * @param[in] c A pointer to the 'codeword' array.
 *
 * @return A negative value if 'a' is greater than 'b', zero if equal, and a positive value if 'a' is less than 'b'.
 */
static int osd_cmp(const void* a, const void* b, void* c)
{
    float* codeword = (float*)c;
    uint8_t aa = *(uint8_t*)a;
    uint8_t bb = *(uint8_t*)b;

    float fabs_a = fabs(codeword[aa]);
    float fabs_b = fabs(codeword[bb]);

    // Reverse order (descending)
    if (fabs_a > fabs_b) return -1;
    if (fabs_a < fabs_b) return 1;
    return 0;
}


/**
 * @brief Ordered statistics decoder for LDPC and new FT8.
 *
 * Decodes a received codeword using the ordered statistics decoder.
 * The decoder sorts the received codeword by strength of the bits,
 * and then reorders the columns of the generator matrix accordingly.
 * Then, it does a gaussian elimination to find a solution to the
 * system of linear equations.
 *
 * @param[in] codeword The received codeword as an array of 174 log-likelihoods. codeword[i] = log ( P(x=0) / P(x=1) )
 * @param[in] depth The maximum number of bits to flip in the received codeword.
 * @param[out] out The decoded plain bits in an array of 91 bits.
 * @param[out] out_depth The actual number of bits that were flipped in the received codeword.
 *
 * @return Returns 1 if the decode was successful, 0 otherwise.
 */
int osd_decode(float codeword[FTX_LDPC_N], int depth, uint8_t out[FTX_LDPC_K], int* out_depth)
{
    const int osd_thresh = -500;

    // sort, strongest first; we'll use strongest 91.
    uint8_t which[FTX_LDPC_N];
    for (int i = 0; i < FTX_LDPC_N; i++)
        which[i] = i;

    qsort_r(which, FTX_LDPC_N, sizeof(uint8_t), osd_cmp, codeword);

    // gen_sys[174 rows][91 cols] has a row per each of the 174 codeword bits,
    // indicating how to generate it by xor with each of the 91 plain bits.

    // generator matrix, reordered strongest codeword bit first.
    uint8_t b[FTX_LDPC_N][FTX_LDPC_K * 2];
    memset(b, 0, sizeof(b));
    for (int i = 0; i < FTX_LDPC_N; i++)
    {
        uint8_t ii = which[i];
        if (ii < FTX_LDPC_K)
        {
                b[i][ii] = 1;
        }
        else
        {
            uint8_t kk = ii - FTX_LDPC_K;
            for (int j = 0; j < FTX_LDPC_K; j++)
            {
                b[i][j] = gen_sys[kk][j];
            }
        }
    }

    if (gauss_jordan(FTX_LDPC_K, FTX_LDPC_N, b, which) == 0)
    {
        return 0;
    }

    uint8_t gen1_inv[FTX_LDPC_K][FTX_LDPC_K];
    for (int i = 0; i < FTX_LDPC_K; i++)
    {
        for (int j = 0; j < FTX_LDPC_K; j++)
        {
            gen1_inv[i][j] = b[i][FTX_LDPC_K + j];
        }
    }

    // y1 is the received bits, same order as gen1_inv,
    // more or less strongest-first, converted from
    // log-likihood to 0/1.
    uint8_t y1[FTX_LDPC_K];
    for (int i = 0; i < FTX_LDPC_K; i++)
    {
        int j = which[i];
        y1[i] = (codeword[j] > 0 ? 1 : 0);
    }

    // can we decode without flipping any bits?
    uint8_t xplain[FTX_LDPC_K];
    matmul(gen1_inv, y1, xplain); // also does mod 2

    float xscore = osd_score(xplain, codeword);
    int ch = osd_check(xplain);
    if (xscore < osd_thresh && ch)
    {
        // just accept this, since no bits had to be flipped.
        memcpy(out, xplain, sizeof(xplain));
        *out_depth = 0;
        return 1;
    }

    uint8_t best_plain[FTX_LDPC_K];
    float best_score = 0;
    int got_a_best = 0;
    int best_depth = -1;

    // flip a few bits, see if decode works.
    for (int ii = 0; ii < depth; ii++)
    {
        int i = FTX_LDPC_K - 1 - ii;
        y1[i] ^= 1;
        matmul(gen1_inv, y1, xplain);
        y1[i] ^= 1;
        float xscore = osd_score(xplain, codeword);
        int ch = osd_check(xplain);
        if (xscore < osd_thresh && ch)
        {
            if (got_a_best == 0 || xscore < best_score)
            {
                got_a_best = 1;
                memcpy(best_plain, xplain, sizeof(best_plain));
                best_score = xscore;
                best_depth = ii;
            }
        }
    }

    if (got_a_best)
    {
        memcpy(out, best_plain, sizeof(best_plain));
        *out_depth = best_depth;
        return 1;
    }
    else
    {
        return 0;
    }
}
