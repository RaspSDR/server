//
// LDPC decoder for FT8.
//
// given a 174-bit codeword as an array of log-likelihood of zero,
// return a 174-bit corrected codeword, or zero-length array.
// last 87 bits are the (systematic) plain-text.
// this is an implementation of the sum-product algorithm
// from Sarah Johnson's Iterative Error Correction book.
// codeword[i] = log ( P(x=0) / P(x=1) )
//

#include "ldpc.h"
#include "constants.h"
#include "fast_math.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>

static int ldpc_check(uint8_t codeword[]);


/**
 * @brief LDPC decoder for FT8.
 *
 * Given a 174-bit codeword as an array of log-likelihood of zero,
 * return a 174-bit corrected codeword, or zero-length array.
 * The last 87 bits are the (systematic) plain-text.
 *
 * This is an implementation of the sum-product algorithm
 * from Sarah Johnson's Iterative Error Correction book.
 *
 * codeword[i] = log ( P(x=0) / P(x=1) )
 *
 * @param[in] codeword 174-bit array of log-likelihood of zero
 * @param[in] max_iters maximum number of iterations
 * @param[out] plain 174-bit array of (hard) decoded bits
 * @param[out] ok number of LDPC errors in the decoded message
 *
 * @return ok number of LDPC errors in the decoded message
 */
void ldpc_decode(const float codeword[], int max_iters, uint8_t plain[], int* ok)
{
    float m[FTX_LDPC_M][FTX_LDPC_N]; // ~60 kB
    float e[FTX_LDPC_M][FTX_LDPC_N]; // ~60 kB
    int min_errors = FTX_LDPC_M;

    for (int j = 0; j < FTX_LDPC_M; j++)
    {
        for (int i = 0; i < FTX_LDPC_N; i++)
        {
            m[j][i] = codeword[i];
            e[j][i] = 0.0f;
        }
    }

    for (int iter = 0; iter < max_iters; iter++)
    {
        for (int j = 0; j < FTX_LDPC_M; j++)
        {
            for (int ii1 = 0; ii1 < kFTX_LDPC_Num_rows[j]; ii1++)
            {
                int i1 = kFTX_LDPC_Nm[j][ii1] - 1;
                if(i1 < 0)
                    continue;

                float a = 1.0f;
                for (int ii2 = 0; ii2 < kFTX_LDPC_Num_rows[j]; ii2++)
                {
                    int i2 = kFTX_LDPC_Nm[j][ii2] - 1;
                    if (i2 >= 0 && i2 != i1)
                    {
                        a *= fast_tanh(-m[j][i2] / 2.0f);
                    }
                }
                e[j][i1] = -2.0f * fast_atanh(a);
            }
        }

        for (int i = 0; i < FTX_LDPC_N; i++)
        {
            float l = codeword[i];
            for (int j = 0; j < 3; j++)
                l += e[kFTX_LDPC_Mn[i][j] - 1][i];
            plain[i] = (l > 0) ? 1 : 0;
        }

        int errors = ldpc_check(plain);

        if (errors < min_errors)
        {
            // Update the current best result
            min_errors = errors;

            if (errors == 0)
            {
                break; // Found a perfect answer
            }
        }

        for (int i = 0; i < FTX_LDPC_N; i++)
        {
            for (int ji1 = 0; ji1 < 3; ji1++)
            {
                int j1 = kFTX_LDPC_Mn[i][ji1] - 1;
                float l = codeword[i];
                for (int ji2 = 0; ji2 < 3; ji2++)
                {
                    if (ji1 != ji2)
                    {
                        int j2 = kFTX_LDPC_Mn[i][ji2] - 1;
                        l += e[j2][i];
                    }
                }
                m[j1][i] = l;
            }
        }
    }

    *ok = min_errors;
}

//
// does a 174-bit codeword pass the FT8's LDPC parity checks?
// returns the number of parity errors.
// 0 means total success.
//
static int ldpc_check(uint8_t codeword[])
{
    int errors = 0;

    for (int m = 0; m < FTX_LDPC_M; ++m)
    {
        uint8_t x = 0;
        for (int i = 0; i < kFTX_LDPC_Num_rows[m]; ++i)
        {
            x ^= codeword[kFTX_LDPC_Nm[m][i] - 1];
        }
        if (x != 0)
        {
            ++errors;
        }
    }
    return errors;
}

/**
 * @brief Belief Propagation (BP) decoding of LDPC code in FT8.
 *
 * @param[in] codeword 174-bit array of log-likelihood of zero
 * @param[in] max_iters maximum number of BP iterations
 * @param[out] plain 174-bit array of (hard) decoded bits
 * @param[out] ok number of LDPC errors in the decoded message
 *
 * This function implements the sum-product algorithm for decoding
 * the LDPC code in FT8. It will run for up to max_iters iterations,
 * and will return the number of LDPC errors in the decoded message.
 * If the number of errors is zero, the decoded message is correct.
 * If the number of errors is non-zero, the decoded message is not
 * guaranteed to be correct.
 *
 * @return ok number of LDPC errors in the decoded message
 */
void bp_decode(const float codeword[], int max_iters, uint8_t plain[], int* ok)
{
    float tov[FTX_LDPC_N][3];
    float toc[FTX_LDPC_M][7];

    int min_errors = FTX_LDPC_M;

    // Precompute index lookup tables
    // nm_midx[m][n_idx]: which of variable node's 3 check indices points back to check m
    // mn_nidx[n][m_idx]: position of variable node n in check node m's neighbor list
    uint8_t nm_midx[FTX_LDPC_M][7];
    uint8_t mn_nidx[FTX_LDPC_N][3];
    for (int m = 0; m < FTX_LDPC_M; ++m)
    {
        for (int n_idx = 0; n_idx < kFTX_LDPC_Num_rows[m]; ++n_idx)
        {
            int n = kFTX_LDPC_Nm[m][n_idx] - 1;
            for (int mi = 0; mi < 3; ++mi)
            {
                if ((kFTX_LDPC_Mn[n][mi] - 1) == m)
                {
                    nm_midx[m][n_idx] = mi;
                    break;
                }
            }
        }
    }
    for (int n = 0; n < FTX_LDPC_N; ++n)
    {
        for (int m_idx = 0; m_idx < 3; ++m_idx)
        {
            int m = kFTX_LDPC_Mn[n][m_idx] - 1;
            for (int ni = 0; ni < kFTX_LDPC_Num_rows[m]; ++ni)
            {
                if ((kFTX_LDPC_Nm[m][ni] - 1) == n)
                {
                    mn_nidx[n][m_idx] = ni;
                    break;
                }
            }
        }
    }

    // initialize message data
    for (int n = 0; n < FTX_LDPC_N; ++n)
    {
        tov[n][0] = tov[n][1] = tov[n][2] = 0;
    }

    // Precomputed sum: codeword[n] + tov[n][0] + tov[n][1] + tov[n][2]
    float tov_sum[FTX_LDPC_N];
    for (int n = 0; n < FTX_LDPC_N; ++n)
    {
        tov_sum[n] = codeword[n];
    }

    for (int iter = 0; iter < max_iters; ++iter)
    {
        // Do a hard decision guess (tov=0 in iter 0)
        int plain_sum = 0;
        for (int n = 0; n < FTX_LDPC_N; ++n)
        {
            plain[n] = (tov_sum[n] > 0) ? 1 : 0;
            plain_sum += plain[n];
        }

        if (plain_sum == 0)
        {
            min_errors = FTX_LDPC_M;
            break;
        }

        // Check to see if we have a codeword
        int errors = ldpc_check(plain);

        if (errors < min_errors)
        {
            min_errors = errors;

            if (errors == 0)
            {
                break;
            }
        }

        // Send messages from bits to check nodes
        // Tnm = tov_sum[n] - tov[n][excluded_m_idx], using precomputed nm_midx
        {
            float tanh_in[8];
            int tanh_m[8], tanh_nidx[8];
            int batch_count = 0;

            for (int m = 0; m < FTX_LDPC_M; ++m)
            {
                for (int n_idx = 0; n_idx < kFTX_LDPC_Num_rows[m]; ++n_idx)
                {
                    int n = kFTX_LDPC_Nm[m][n_idx] - 1;
                    float Tnm = tov_sum[n] - tov[n][nm_midx[m][n_idx]];

                    tanh_in[batch_count] = -Tnm / 2;
                    tanh_m[batch_count] = m;
                    tanh_nidx[batch_count] = n_idx;
                    batch_count++;

                    if (batch_count == 4)
                    {
                        float tanh_out[4];
                        fast_tanh4(tanh_out, tanh_in);
                        for (int k = 0; k < 4; ++k)
                            toc[tanh_m[k]][tanh_nidx[k]] = tanh_out[k];
                        batch_count = 0;
                    }
                }
            }
            for (int k = 0; k < batch_count; ++k)
                toc[tanh_m[k]][tanh_nidx[k]] = fast_tanh(tanh_in[k]);
        }

        // Send messages from check nodes to variable nodes
        // Use row-local prefix/suffix products to get product-excluding-one in O(1)
        for (int m = 0; m < FTX_LDPC_M; ++m)
        {
            int nrw = kFTX_LDPC_Num_rows[m];
            float fwd[8], bwd[8];
            fwd[0] = 1.0f;
            for (int i = 0; i < nrw; ++i)
                fwd[i + 1] = fwd[i] * toc[m][i];
            bwd[nrw] = 1.0f;
            for (int i = nrw - 1; i >= 0; --i)
                bwd[i] = bwd[i + 1] * toc[m][i];

            for (int n_idx = 0; n_idx < nrw; ++n_idx)
            {
                int n = kFTX_LDPC_Nm[m][n_idx] - 1;
                int m_idx = nm_midx[m][n_idx];
                float Tmn = fwd[n_idx] * bwd[n_idx + 1];
                tov[n][m_idx] = -2 * fast_atanh(Tmn);
            }
        }

        // Update tov_sum
        for (int n = 0; n < FTX_LDPC_N; ++n)
        {
            tov_sum[n] = codeword[n] + tov[n][0] + tov[n][1] + tov[n][2];
        }
    }

    *ok = min_errors;
}

/**
 * @brief Encodes a 91-bit message into a 174-bit LDPC codeword.
 *
 * This function mimics the encoding process from WSJT-X's encode174_91.f90.
 * It takes a 91-bit plain-text message and produces a 174-bit codeword
 * by appending 83 bits of redundancy, computed using a systematic generator matrix.
 *
 * @param[in] plain An array of 91 bits representing the plain-text message.
 * @param[out] codeword An array of 174 bits representing the encoded LDPC codeword.
 */
void ldpc_encode(const uint8_t plain[FTX_LDPC_K], uint8_t codeword[FTX_LDPC_N])
{
    // plain is 91 bits of plain-text.
    // returns a 174-bit codeword.
    // mimics wsjt-x's encode174_91.f90.

    // the systematic 91 bits.
    for (int i = 0; i < FTX_LDPC_K; i++)
    {
        codeword[i] = plain[i];
    }

    // the 174-91 bits of redundancy.
    for (int i = 0; i < FTX_LDPC_M; i++)
    {
        int sum = 0;
        for (int j = 0; j < FTX_LDPC_K; j++)
        {
            sum += gen_sys[i][j] & plain[j];
            codeword[i + FTX_LDPC_K] = sum % 2;
        }
    }
}
