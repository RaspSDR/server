//
// LDPC encoder/decoder for FST4 and FST4W.
//
// FST4 uses (240,101) LDPC code: 101 info bits, 139 parity bits, 240 total.
// FST4W uses (240,74) LDPC code: 74 info bits, 166 parity bits, 240 total.
// Both codes have column weight 3 (each bit participates in exactly 3 checks).
//
// The BP decoder follows the same sum-product algorithm as the FT8 decoder
// in ldpc.c, adapted for the different code dimensions.
//
// Note: FST4 Mn/Nm arrays are 0-indexed (unlike FT8's 1-indexed arrays).
//

#include "fst4_ldpc.h"
#include "constants.h"
#include "fast_math.h"

#include <math.h>
#include <stdbool.h>

static uint8_t parity8(uint8_t x)
{
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return x & 1;
}

// Generic bitpacked LDPC encoder: info_bits → codeword using generator matrix
static void encode240(const uint8_t* message, uint8_t* codeword,
                      const uint8_t* generator, int k, int k_bytes, int m)
{
    int n_bytes = (k + m + 7) / 8;

    // Copy message bits into codeword, zero-fill the rest
    for (int j = 0; j < n_bytes; ++j)
    {
        codeword[j] = (j < k_bytes) ? message[j] : 0;
    }

    // Compute parity bits using the generator matrix
    uint8_t col_mask = (0x80u >> (k % 8u));
    uint8_t col_idx = k_bytes - 1;

    for (int i = 0; i < m; ++i)
    {
        const uint8_t* gen_row = generator + (i * k_bytes);
        uint8_t nsum = 0;
        for (int j = 0; j < k_bytes; ++j)
        {
            nsum ^= parity8(message[j] & gen_row[j]);
        }

        if (nsum & 1)
        {
            codeword[col_idx] |= col_mask;
        }

        col_mask >>= 1;
        if (col_mask == 0)
        {
            col_mask = 0x80u;
            ++col_idx;
        }
    }
}

void fst4_ldpc_encode(const uint8_t* message, uint8_t* codeword)
{
    encode240(message, codeword,
              (const uint8_t*)kFST4_LDPC_generator,
              FST4_LDPC_K, FST4_LDPC_K_BYTES, FST4_LDPC_M);
}

void fst4w_ldpc_encode(const uint8_t* message, uint8_t* codeword)
{
    encode240(message, codeword,
              (const uint8_t*)kFST4W_LDPC_generator,
              FST4W_LDPC_K, FST4W_LDPC_K_BYTES, FST4W_LDPC_M);
}

// Generic BP decoder for (240,K) LDPC codes
// Mn/Nm arrays are 0-indexed.
static void bp_decode_240(const float codeword[], int max_iters,
                          uint8_t plain[], int* ok,
                          int n, int m, int max_nrw,
                          const uint16_t* mn,     // [n][3]
                          const uint16_t* nm,     // [m][max_nrw]
                          const uint8_t* num_rows) // [m]
{
    float tov[FST4_LDPC_N][3];
    float toc[FST4W_LDPC_M][FST4_LDPC_MAX_NRW]; // Use max dimensions

    int min_errors = m;

    // Precompute index lookup tables
    // nm_midx[j][n_idx]: which of variable node's 3 check indices points back to check j
    // mn_nidx[i][m_idx]: position of variable node i in check node j's neighbor list
    uint8_t nm_midx[FST4W_LDPC_M][FST4_LDPC_MAX_NRW];
    uint8_t mn_nidx[FST4_LDPC_N][3];
    for (int j = 0; j < m; ++j)
    {
        int nrw = num_rows[j];
        for (int n_idx = 0; n_idx < nrw; ++n_idx)
        {
            int ni = nm[j * max_nrw + n_idx];
            for (int mi = 0; mi < 3; ++mi)
            {
                if (mn[ni * 3 + mi] == (uint16_t)j)
                {
                    nm_midx[j][n_idx] = mi;
                    break;
                }
            }
        }
    }
    for (int i = 0; i < n; ++i)
    {
        for (int m_idx = 0; m_idx < 3; ++m_idx)
        {
            int j = mn[i * 3 + m_idx];
            int nrw = num_rows[j];
            for (int ni = 0; ni < nrw; ++ni)
            {
                if (nm[j * max_nrw + ni] == (uint16_t)i)
                {
                    mn_nidx[i][m_idx] = ni;
                    break;
                }
            }
        }
    }

    for (int i = 0; i < n; ++i)
    {
        tov[i][0] = tov[i][1] = tov[i][2] = 0;
    }

    // Precomputed sum: codeword[i] + tov[i][0] + tov[i][1] + tov[i][2]
    float tov_sum[FST4_LDPC_N];
    for (int i = 0; i < n; ++i)
    {
        tov_sum[i] = codeword[i];
    }

    for (int iter = 0; iter < max_iters; ++iter)
    {
        // Hard decision
        int plain_sum = 0;
        for (int i = 0; i < n; ++i)
        {
            plain[i] = (tov_sum[i] > 0) ? 1 : 0;
            plain_sum += plain[i];
        }

        if (plain_sum == 0)
        {
            min_errors = m;
            break;
        }

        // Parity check
        int errors = 0;
        for (int j = 0; j < m; ++j)
        {
            uint8_t x = 0;
            int nrw = num_rows[j];
            for (int i = 0; i < nrw; ++i)
            {
                x ^= plain[nm[j * max_nrw + i]];
            }
            if (x != 0)
                ++errors;
        }

        if (errors < min_errors)
        {
            min_errors = errors;
            if (errors == 0)
                break;
        }

        // Messages from bits to check nodes (batch tanh)
        // Tnm = tov_sum[ni] - tov[ni][excluded_m_idx], using precomputed nm_midx
        {
            float tanh_in[4];
            int tanh_j[4], tanh_nidx[4];
            int batch_count = 0;

            for (int j = 0; j < m; ++j)
            {
                int nrw = num_rows[j];
                for (int n_idx = 0; n_idx < nrw; ++n_idx)
                {
                    int ni = nm[j * max_nrw + n_idx];
                    float Tnm = tov_sum[ni] - tov[ni][nm_midx[j][n_idx]];

                    tanh_in[batch_count] = -Tnm / 2;
                    tanh_j[batch_count] = j;
                    tanh_nidx[batch_count] = n_idx;
                    batch_count++;

                    if (batch_count == 4)
                    {
                        float tanh_out[4];
                        fast_tanh4(tanh_out, tanh_in);
                        for (int k = 0; k < 4; ++k)
                            toc[tanh_j[k]][tanh_nidx[k]] = tanh_out[k];
                        batch_count = 0;
                    }
                }
            }
            for (int k = 0; k < batch_count; ++k)
                toc[tanh_j[k]][tanh_nidx[k]] = fast_tanh(tanh_in[k]);
        }

        // Messages from check nodes to variable nodes
        // Row-local prefix/suffix products for product-excluding-one in O(1)
        for (int j = 0; j < m; ++j)
        {
            int nrw = num_rows[j];
            float fwd[8], bwd[8];
            fwd[0] = 1.0f;
            for (int i = 0; i < nrw; ++i)
                fwd[i + 1] = fwd[i] * toc[j][i];
            bwd[nrw] = 1.0f;
            for (int i = nrw - 1; i >= 0; --i)
                bwd[i] = bwd[i + 1] * toc[j][i];

            for (int n_idx = 0; n_idx < nrw; ++n_idx)
            {
                int ni = nm[j * max_nrw + n_idx];
                int m_idx = nm_midx[j][n_idx];
                float Tmn = fwd[n_idx] * bwd[n_idx + 1];
                tov[ni][m_idx] = -2 * fast_atanh(Tmn);
            }
        }

        // Update tov_sum
        for (int i = 0; i < n; ++i)
        {
            tov_sum[i] = codeword[i] + tov[i][0] + tov[i][1] + tov[i][2];
        }
    }

    *ok = min_errors;
}

void fst4_ldpc_decode(const float codeword[], int max_iters, uint8_t plain[], int* ok)
{
    bp_decode_240(codeword, max_iters, plain, ok,
                  FST4_LDPC_N, FST4_LDPC_M, FST4_LDPC_MAX_NRW,
                  (const uint16_t*)kFST4_LDPC_Mn,
                  (const uint16_t*)kFST4_LDPC_Nm,
                  kFST4_LDPC_num_rows);
}

void fst4w_ldpc_decode(const float codeword[], int max_iters, uint8_t plain[], int* ok)
{
    bp_decode_240(codeword, max_iters, plain, ok,
                  FST4_LDPC_N, FST4W_LDPC_M, FST4W_LDPC_MAX_NRW,
                  (const uint16_t*)kFST4W_LDPC_Mn,
                  (const uint16_t*)kFST4W_LDPC_Nm,
                  kFST4W_LDPC_num_rows);
}
