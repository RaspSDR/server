#include "decode_internal.h"
#include "constants.h"

int fst4_sync_score(const ftx_waterfall_t* wf, const ftx_candidate_t* candidate)
{
    int score = 0;
    int num_average = 0;

    const WF_ELEM_T* mag_cand = get_cand_mag(wf, candidate);

    // FST4 sync: S8 at positions 0, 38, 76, 114, 152
    // sync_word1 at 0, 76, 152; sync_word2 at 38, 114
    const int sync_pos[5] = { 0, 38, 76, 114, 152 };
    const uint8_t* sync_words[5] = {
        kFST4_Sync_word1, kFST4_Sync_word2,
        kFST4_Sync_word1, kFST4_Sync_word2,
        kFST4_Sync_word1
    };

    for (int m = 0; m < FST4_NUM_SYNC; ++m)
    {
        for (int k = 0; k < FST4_LENGTH_SYNC; ++k)
        {
            int block = sync_pos[m] + k;
            int block_abs = candidate->time_offset + block;
            if (block_abs < 0)
                continue;
            if (block_abs >= wf->num_blocks)
                break;

            const WF_ELEM_T* p4 = mag_cand + (block * wf->block_stride);
            int sm = sync_words[m][k];

            // Compare expected tone to average of all tones
            score += (4 * WF_ELEM_MAG_INT(p4[sm]))
                   - WF_ELEM_MAG_INT(p4[0]) - WF_ELEM_MAG_INT(p4[1])
                   - WF_ELEM_MAG_INT(p4[2]) - WF_ELEM_MAG_INT(p4[3]);
            num_average += 4;
        }
    }

    if (num_average > 0)
        score /= num_average;

    return score;
}

void fst4_extract_likelihood(const ftx_waterfall_t* wf, const ftx_candidate_t* cand, float* log240)
{
    const WF_ELEM_T* mag = get_cand_mag(wf, cand);

    // FST4 frame: S8 D30 S8 D30 S8 D30 S8 D30 S8
    // Data symbol positions: 8-37, 46-75, 84-113, 122-151
    const int data_start[4] = { 8, 46, 84, 122 };

    int bit_idx = 0;
    for (int d = 0; d < 4; ++d)
    {
        for (int k = 0; k < 30; ++k)
        {
            int sym_idx = data_start[d] + k;
            int block = cand->time_offset + sym_idx;

            if ((block < 0) || (block >= wf->num_blocks))
            {
                log240[bit_idx + 0] = 0;
                log240[bit_idx + 1] = 0;
            }
            else
            {
                ft4_extract_symbol(mag + (sym_idx * wf->block_stride), log240 + bit_idx);
            }
            bit_idx += 2;
        }
    }
}
