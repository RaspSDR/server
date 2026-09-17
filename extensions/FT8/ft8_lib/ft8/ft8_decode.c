#include "decode_internal.h"
#include "constants.h"

// Compute unnormalized log likelihood log(p(1) / p(0)) of 3 message bits (1 FSK symbol)
static void ft8_extract_symbol(const WF_ELEM_T* wf, float* logl)
{
    float s2[8];

    for (int j = 0; j < 8; ++j)
    {
        s2[j] = WF_ELEM_MAG(wf[kFT8_Gray_map[j]]);
    }

    logl[0] = max4(s2[4], s2[5], s2[6], s2[7]) - max4(s2[0], s2[1], s2[2], s2[3]);
    logl[1] = max4(s2[2], s2[3], s2[6], s2[7]) - max4(s2[0], s2[1], s2[4], s2[5]);
    logl[2] = max4(s2[1], s2[3], s2[5], s2[7]) - max4(s2[0], s2[2], s2[4], s2[6]);
}

int ft8_sync_score(const ftx_waterfall_t* wf, const ftx_candidate_t* candidate)
{
    int score = 0;
    int num_average = 0;

    // Get the pointer to symbol 0 of the candidate
    const WF_ELEM_T* mag_cand = get_cand_mag(wf, candidate);

    // Compute average score over sync symbols (m+k = 0-7, 36-43, 72-79)
    for (int m = 0; m < FT8_NUM_SYNC; ++m)
    {
        for (int k = 0; k < FT8_LENGTH_SYNC; ++k)
        {
            int block = (FT8_SYNC_OFFSET * m) + k;          // relative to the message
            int block_abs = candidate->time_offset + block; // relative to the captured signal
            // Check for time boundaries
            if (block_abs < 0)
                continue;
            if (block_abs >= wf->num_blocks)
                break;

            // Get the pointer to symbol 'block' of the candidate
            const WF_ELEM_T* p8 = mag_cand + (block * wf->block_stride);

            // Check only the neighbors of the expected symbol frequency- and time-wise
            int sm = kFT8_Costas_pattern[k]; // Index of the expected bin
            if (sm > 0)
            {
                // look at one frequency bin lower
                score += WF_ELEM_MAG_INT(p8[sm]) - WF_ELEM_MAG_INT(p8[sm - 1]);
                ++num_average;
            }
            if (sm < 7)
            {
                // look at one frequency bin higher
                score += WF_ELEM_MAG_INT(p8[sm]) - WF_ELEM_MAG_INT(p8[sm + 1]);
                ++num_average;
            }
            if ((k > 0) && (block_abs > 0))
            {
                // look one symbol back in time
                score += WF_ELEM_MAG_INT(p8[sm]) - WF_ELEM_MAG_INT(p8[sm - wf->block_stride]);
                ++num_average;
            }
            if (((k + 1) < FT8_LENGTH_SYNC) && ((block_abs + 1) < wf->num_blocks))
            {
                // look one symbol forward in time
                score += WF_ELEM_MAG_INT(p8[sm]) - WF_ELEM_MAG_INT(p8[sm + wf->block_stride]);
                ++num_average;
            }
        }
    }

    if (num_average > 0)
        score /= num_average;

    return score;
}

void ft8_extract_likelihood(const ftx_waterfall_t* wf, const ftx_candidate_t* cand, float* log174)
{
    const WF_ELEM_T* mag = get_cand_mag(wf, cand); // Pointer to 8 magnitude bins of the first symbol

    // Go over FSK tones and skip Costas sync symbols
    for (int k = 0; k < FT8_ND; ++k)
    {
        // Skip either 7 or 14 sync symbols
        int sym_idx = k + ((k < 29) ? 7 : 14);
        int bit_idx = 3 * k;

        // Check for time boundaries
        int block = cand->time_offset + sym_idx;
        if ((block < 0) || (block >= wf->num_blocks))
        {
            log174[bit_idx + 0] = 0;
            log174[bit_idx + 1] = 0;
            log174[bit_idx + 2] = 0;
        }
        else
        {
            ft8_extract_symbol(mag + (sym_idx * wf->block_stride), log174 + bit_idx);
        }
    }
}
