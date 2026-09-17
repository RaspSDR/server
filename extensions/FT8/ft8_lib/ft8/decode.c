#include "decode.h"
#include "decode_internal.h"
#include "encode.h"
#include "constants.h"
#include "crc.h"
#include "ldpc.h"
#include "fst4_ldpc.h"
#include "osd.h"

#include <stdbool.h>
#include <math.h>

#define LOG_LEVEL LOG_INFO
#include "debug.h"

#define kMaxLDPCErrors 32

// Protocol-specific functions (defined in ft8_decode.c, ft4_decode.c, fst4_decode.c)
int ft8_sync_score(const ftx_waterfall_t* wf, const ftx_candidate_t* candidate);
int ft4_sync_score(const ftx_waterfall_t* wf, const ftx_candidate_t* candidate);
int fst4_sync_score(const ftx_waterfall_t* wf, const ftx_candidate_t* candidate);
void ft8_extract_likelihood(const ftx_waterfall_t* wf, const ftx_candidate_t* cand, float* log174);
void ft4_extract_likelihood(const ftx_waterfall_t* wf, const ftx_candidate_t* cand, float* log174);
void fst4_extract_likelihood(const ftx_waterfall_t* wf, const ftx_candidate_t* cand, float* log240);

static void pack_bits(const uint8_t bit_array[], int num_bits, uint8_t packed[]);
static void heapify_down(ftx_candidate_t heap[], int heap_size);
static void heapify_up(ftx_candidate_t heap[], int heap_size);
static void ftx_normalize_logl(float* logl, int n);

// Wrapper: ftx_extract_crc returns uint16_t, but descriptor needs uint32_t
static uint32_t ftx_extract_crc_u32(const uint8_t* packed)
{
    return (uint32_t)ftx_extract_crc(packed);
}

// Wrapper: osd_decode has matching signature but we need a NULL-safe typedef
// (osd_decode is declared in osd.h, used directly via function pointer)

// Static protocol descriptor instances
static const ftx_protocol_desc_t kFT8_desc = {
    .protocol = FTX_PROTOCOL_FT8,
    .num_tones = 8,
    .num_symbols = FT8_NN,
    .ldpc_n = FTX_LDPC_N,
    .ldpc_k = FTX_LDPC_K,
    .ldpc_k_bytes = FTX_LDPC_K_BYTES,
    .max_ldpc_iterations = 25,
    .use_rect_window = false,
    .xor_payload = false,
    .sync_score = ft8_sync_score,
    .extract_likelihood = ft8_extract_likelihood,
    .ldpc_decode = bp_decode,
    .osd_decode = osd_decode,
    .check_crc = ftx_check_crc_packed,
    .extract_crc = ftx_extract_crc_u32,
    .encode = ft8_encode,
};

static const ftx_protocol_desc_t kFT4_desc = {
    .protocol = FTX_PROTOCOL_FT4,
    .num_tones = 4,
    .num_symbols = FT4_NN,
    .ldpc_n = FTX_LDPC_N,
    .ldpc_k = FTX_LDPC_K,
    .ldpc_k_bytes = FTX_LDPC_K_BYTES,
    .max_ldpc_iterations = 25,
    .use_rect_window = false,
    .xor_payload = true,
    .sync_score = ft4_sync_score,
    .extract_likelihood = ft4_extract_likelihood,
    .ldpc_decode = bp_decode,
    .osd_decode = osd_decode,
    .check_crc = ftx_check_crc_packed,
    .extract_crc = ftx_extract_crc_u32,
    .encode = ft4_encode,
};

static const ftx_protocol_desc_t kFST4_desc = {
    .protocol = FTX_PROTOCOL_FST4,
    .num_tones = 4,
    .num_symbols = FST4_NN,
    .ldpc_n = FST4_LDPC_N,
    .ldpc_k = FST4_LDPC_K,
    .ldpc_k_bytes = FST4_LDPC_K_BYTES,
    .max_ldpc_iterations = 100,
    .use_rect_window = true,
    .xor_payload = true,
    .sync_score = fst4_sync_score,
    .extract_likelihood = fst4_extract_likelihood,
    .ldpc_decode = fst4_ldpc_decode,
    .osd_decode = NULL, // OSD hardcoded to (174,91); FST4 uses (240,101)
    .check_crc = fst4_check_crc,
    .extract_crc = fst4_extract_crc,
    .encode = fst4_encode,
};

static const ftx_protocol_desc_t kFST4W_desc = {
    .protocol = FTX_PROTOCOL_FST4W,
    .num_tones = 4,
    .num_symbols = FST4_NN,
    .ldpc_n = FST4_LDPC_N,
    .ldpc_k = FST4W_LDPC_K,
    .ldpc_k_bytes = FST4W_LDPC_K_BYTES,
    .max_ldpc_iterations = 100,
    .use_rect_window = true,
    .xor_payload = false,
    .sync_score = fst4_sync_score,
    .extract_likelihood = fst4_extract_likelihood,
    .ldpc_decode = fst4w_ldpc_decode,
    .osd_decode = NULL, // OSD hardcoded to (174,91); FST4W uses (240,74)
    .check_crc = fst4w_check_crc,
    .extract_crc = fst4w_extract_crc,
    .encode = fst4w_encode,
};

const ftx_protocol_desc_t* ftx_protocol_get_desc(ftx_protocol_t protocol)
{
    switch (protocol)
    {
    case FTX_PROTOCOL_FT4:
        return &kFT4_desc;
    case FTX_PROTOCOL_FST4:
        return &kFST4_desc;
    case FTX_PROTOCOL_FST4W:
        return &kFST4W_desc;
    case FTX_PROTOCOL_FT8:
    default:
        return &kFT8_desc;
    }
}

int ftx_find_candidates(const ftx_waterfall_t* wf, int num_candidates, ftx_candidate_t heap[], int min_score)
{
    const ftx_protocol_desc_t* desc = wf->desc;

    int heap_size = 0;
    ftx_candidate_t candidate;

    // Here we allow time offsets that exceed signal boundaries, as long as we still have all data bits.
    // I.e. we can afford to skip the first 7 or the last 7 Costas symbols, as long as we track how many
    // sync symbols we included in the score, so the score is averaged.
    for (candidate.time_sub = 0; candidate.time_sub < wf->time_osr; ++candidate.time_sub)
    {
        for (candidate.freq_sub = 0; candidate.freq_sub < wf->freq_osr; ++candidate.freq_sub)
        {
            for (candidate.time_offset = -10; candidate.time_offset < 20; ++candidate.time_offset)
            {
                for (candidate.freq_offset = 0; (candidate.freq_offset + desc->num_tones - 1) < wf->num_bins; ++candidate.freq_offset)
                {
                    candidate.score = desc->sync_score(wf, &candidate);

                    if (candidate.score < min_score)
                        continue;

                    // If the heap is full AND the current candidate is better than
                    // the worst in the heap, we remove the worst and make space
                    if ((heap_size == num_candidates) && (candidate.score > heap[0].score))
                    {
                        --heap_size;
                        heap[0] = heap[heap_size];
                        heapify_down(heap, heap_size);
                    }

                    // If there's free space in the heap, we add the current candidate
                    if (heap_size < num_candidates)
                    {
                        heap[heap_size] = candidate;
                        ++heap_size;
                        heapify_up(heap, heap_size);
                    }
                }
            }
        }
    }

    // Sort the candidates by sync strength - here we benefit from the heap structure
    int len_unsorted = heap_size;
    while (len_unsorted > 1)
    {
        // Take the top (index 0) element which is guaranteed to have the smallest score,
        // exchange it with the last element in the heap, and decrease the heap size.
        // Then restore the heap property in the new, smaller heap.
        // At the end the elements will be sorted in descending order.
        ftx_candidate_t tmp = heap[len_unsorted - 1];
        heap[len_unsorted - 1] = heap[0];
        heap[0] = tmp;
        len_unsorted--;
        heapify_down(heap, len_unsorted);
    }

    return heap_size;
}

static void ftx_normalize_logl(float* logl, int n)
{
    // Compute the variance of logl
    float sum = 0;
    float sum2 = 0;
    for (int i = 0; i < n; ++i)
    {
        sum += logl[i];
        sum2 += logl[i] * logl[i];
    }
    float inv_n = 1.0f / n;
    float variance = (sum2 - (sum * sum * inv_n)) * inv_n;

    // Normalize logl distribution and scale it with experimentally found coefficient
    float norm_factor = sqrtf(24.0f / variance);
    for (int i = 0; i < n; ++i)
    {
        logl[i] *= norm_factor;
    }
}

/**
 * @brief Subtract the estimated noise from the signal, given a candidate and a sequence of tones
 *
 * This function takes a candidate and a sequence of tones, and subtracts the estimated noise from the signal.
 * The noise is estimated as the minimum signal power of all tones except the one of the candidate.
 * The signal power is then subtracted from the signal.
 *
 * @param wf Waterfall data
 * @param candidate Candidate to subtract
 * @param tones Sequence of tones
 * @param n_tones Length of sequence of tones
 */
float ftx_substract(const ftx_waterfall_t* wf, const ftx_candidate_t* candidate, uint8_t* tones, uint8_t n_tones)
{
    int n_items = wf->desc->num_tones;

    ftx_candidate_t can = *candidate;
    float snr_all = 0;

    for (int freq_sub = 0; freq_sub < wf->freq_osr; freq_sub++)
    {
        can.freq_sub = freq_sub;

        const WF_ELEM_T* mag_cand = get_cand_mag(wf, &can);
        float noise = 0;
        float signal = 0;
        int num_average = 0;

        for (int i = 0; i < n_tones; i++)
        {

            int block_abs = candidate->time_offset + i; // relative to the captured signal
            // Check for time boundaries
            if (block_abs < 0)
                continue;
            if (block_abs >= wf->num_blocks)
                break;

            // Get the pointer to symbol 'block' of the candidate
            const WF_ELEM_T* wf_el = mag_cand + (i * wf->block_stride);

            float noise_val = 100000.0;
            for (int s = 0; s < n_items; s++)
            {
                if (s == tones[i])
                    continue;
                if (WF_ELEM_MAG(wf_el[s]) < noise_val)
                    noise_val = WF_ELEM_MAG(wf_el[s]);
            }
            noise += noise_val;
            signal += WF_ELEM_MAG(wf_el[tones[i]]);
            num_average++;
        }

        noise /= num_average;
        signal /= num_average;
        float snr = signal - noise;

        for (int i = 0; i < n_tones; i++)
        {

            int block_abs = candidate->time_offset + i; // relative to the captured signal
            // Check for time boundaries
            if (block_abs < 0)
                continue;
            if (block_abs >= wf->num_blocks)
                break;

            // Get the pointer to symbol 'block' of the candidate
            WF_ELEM_T* wf_el = (WF_ELEM_T*)mag_cand + (i * wf->block_stride);

            SUB_WF_ELEM_MAG(wf_el[tones[i]], snr);
        }

        snr_all += snr;
        LOG(LOG_INFO, "Freq: %d Noise: %f, Signal: %f, SNR: %f score: %d\n", candidate->freq_offset, noise, signal, snr, candidate->score);
    }

    return snr_all / wf->freq_osr;
}

bool ftx_decode_candidate(const ftx_waterfall_t* wf, const ftx_candidate_t* cand, int max_iterations, ftx_message_t* message, ftx_decode_status_t* status)
{
    LOG(LOG_INFO, "Freq: %d score: %d time_off: %d time_sub: %d freq_sub: %d\n", cand->freq_offset, cand->score, cand->time_offset, cand->time_sub, cand->freq_sub);

    const ftx_protocol_desc_t* desc = wf->desc;

    // Step 1: Extract log-likelihoods
    float log_llr[FST4_LDPC_N]; // max size across all protocols
    desc->extract_likelihood(wf, cand, log_llr);

    // Step 2: Normalize
    ftx_normalize_logl(log_llr, desc->ldpc_n);

    // Step 3: LDPC decode
    uint8_t plain_bits[FST4_LDPC_N]; // max size
    desc->ldpc_decode(log_llr, max_iterations, plain_bits, &status->ldpc_errors);

    // Step 4: OSD fallback (only if descriptor provides an OSD function)
    if (status->ldpc_errors > 0)
    {
        if (desc->osd_decode != NULL && status->ldpc_errors <= kMaxLDPCErrors)
        {
            int got_depth = -1;
            if (!desc->osd_decode(log_llr, 6, plain_bits, &got_depth))
                return false;
        }
        else
        {
            return false;
        }
    }

    // Step 5: Pack bits
    uint8_t packed[FST4_LDPC_K_BYTES]; // max size
    pack_bits(plain_bits, desc->ldpc_k, packed);

    // Step 6: Check CRC
    status->crc_valid = desc->check_crc(packed);
    if (!status->crc_valid)
        return false;

    // Step 7: Extract CRC as message hash
    message->hash = (uint16_t)desc->extract_crc(packed);

    // Step 8: Extract payload (XOR if needed)
    if (desc->xor_payload)
    {
        for (int i = 0; i < 10; ++i)
            message->payload[i] = packed[i] ^ kFT4_XOR_sequence[i];
    }
    else if (desc->ldpc_k_bytes < 10)
    {
        // FST4W: fewer info bytes than the 10-byte payload buffer
        for (int i = 0; i < desc->ldpc_k_bytes; ++i)
            message->payload[i] = packed[i];
        for (int i = desc->ldpc_k_bytes; i < 10; ++i)
            message->payload[i] = 0;
    }
    else
    {
        for (int i = 0; i < 10; ++i)
            message->payload[i] = packed[i];
    }

    // Step 9: Re-encode and subtract
    uint8_t tones[FST4_NN]; // max size
    desc->encode(message->payload, tones);
    message->snr = ftx_substract(wf, cand, tones, desc->num_symbols);

    return true;
}
static void heapify_down(ftx_candidate_t heap[], int heap_size)
{
    // heapify from the root down
    int current = 0; // root node
    while (true)
    {
        int left = 2 * current + 1;
        int right = left + 1;

        // Find the smallest value of (parent, left child, right child)
        int smallest = current;
        if ((left < heap_size) && (heap[left].score < heap[smallest].score))
        {
            smallest = left;
        }
        if ((right < heap_size) && (heap[right].score < heap[smallest].score))
        {
            smallest = right;
        }

        if (smallest == current)
        {
            break;
        }

        // Exchange the current node with the smallest child and move down to it
        ftx_candidate_t tmp = heap[smallest];
        heap[smallest] = heap[current];
        heap[current] = tmp;
        current = smallest;
    }
}

static void heapify_up(ftx_candidate_t heap[], int heap_size)
{
    // heapify from the last node up
    int current = heap_size - 1;
    while (current > 0)
    {
        int parent = (current - 1) / 2;
        if (!(heap[current].score < heap[parent].score))
        {
            break;
        }

        // Exchange the current node with its parent and move up
        ftx_candidate_t tmp = heap[parent];
        heap[parent] = heap[current];
        heap[current] = tmp;
        current = parent;
    }
}

// Packs a string of bits each represented as a zero/non-zero byte in plain[],
// as a string of packed bits starting from the MSB of the first byte of packed[]
static void pack_bits(const uint8_t bit_array[], int num_bits, uint8_t packed[])
{
    int num_bytes = (num_bits + 7) / 8;
    for (int i = 0; i < num_bytes; ++i)
    {
        packed[i] = 0;
    }

    uint8_t mask = 0x80;
    int byte_idx = 0;
    for (int i = 0; i < num_bits; ++i)
    {
        if (bit_array[i])
        {
            packed[byte_idx] |= mask;
        }
        mask >>= 1;
        if (!mask)
        {
            mask = 0x80;
            ++byte_idx;
        }
    }
}