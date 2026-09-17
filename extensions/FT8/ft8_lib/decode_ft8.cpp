
#include "types.h"
#include "config.h"
#include "str.h"
#include "rx/CuteSDR/datatypes.h"
#include "rx/rx.h"
#include "coroutines.h"
#include "FT8.h"
#include "PSKReporter.h"
#include "printf.h"
#include "ext.h"
#include "mqttpub.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>

#include "ft8/decode.h"
#include "ft8/encode.h"
#include "ft8/message.h"

#include "common/common.h"
#include "common/monitor.h"

#define LOG_LEVEL LOG_WARN
#include "ft8/debug.h"

#ifdef PR_USE_CALLSIGN_HASHTABLE
    #define CALLSIGN_HASHTABLE_MAX 1024
    #define CALLSIGN_AGE_MAX 60             // one hour
    //#define CALLSIGN_HASHTABLE_MAX 64
    //#define CALLSIGN_AGE_MAX 15
#else
    #define CALLSIGN_HASHTABLE_MAX 256
    #define CALLSIGN_AGE_MAX 3
#endif

#define kMin_score 10 // Minimum sync score threshold for candidates
#define kMax_candidates 140
#define kLDPC_iterations 25

#define kMax_decoded_messages 140

const int kFreq_osr = 2; // Frequency oversampling rate (bin subdivision)
const int kTime_osr = 4; // Time oversampling rate (symbol subdivision)

typedef struct {
    // NB: callsign is NOT null terminated (to save a byte). Must use strncpy() / strncmp()
    char callsign[11]; ///> Up to 11 symbols of callsign + trailing zeros (always filled)
    u1_t uploaded;
    uint32_t hash;     ///> 10 MSBs contain the age of callsign; 22 LSBs contain hash value

} __attribute__((packed)) callsign_hashtable_t;

typedef struct {
    monitor_t mon;
    struct tm tm_slot_start;

    u4_t decode_time;

} frame_ft8_t;

#define FRAME_SIZE 2

typedef struct {
    u4_t magic;
    int rx_chan;
    bool init;
    bool debug;
    int freqHz;

    ftx_protocol_t protocol;
    int tr_period;
    char protocol_s[12];
    int have_call_and_grid;

    float slot_period;
    int num_samples;
    TYPEREAL *samples;
    monitor_shared_t mon_shared;
    monitor_stream_t mon_stream;
    bool streaming;
    bool tsync;
    int in_pos;
    int frame_pos;
    u4_t slot;

    frame_ft8_t frames[FRAME_SIZE];
    int frame_idx_fill;
    int frame_idx_decode;

    callsign_hashtable_t *callsign_hashtable;
    int callsign_hashtable_size;

    ftx_candidate_t candidate_list[kMax_candidates];
    uint64_t padding0;
    ftx_message_t decoded[kMax_decoded_messages];
    uint64_t padding1;
    ftx_message_t* decoded_hashtable[kMax_decoded_messages];
    uint64_t padding2;

    tid_t compute_task;
} decode_ft8_t;

// #define CHECK_PADDING(ft8) do { if (ft8->padding0 != 0xdeadbeec || ft8->padding1 != 0xdeadbeed || ft8->padding2 != 0xdeadbeee) { panic("FT8 padding error"); } } while (0)
#define CHECK_PADDING(ft8) do {} while(0)

static decode_ft8_t decode_ft8[MAX_RX_CHANS];

static void hashtable_init(int rx_chan)
{
    decode_ft8_t *ft8 = &decode_ft8[rx_chan];
    ft8->callsign_hashtable_size = 0;
    ft8->callsign_hashtable = (callsign_hashtable_t *) malloc(sizeof(callsign_hashtable_t) * CALLSIGN_HASHTABLE_MAX);
    memset(ft8->callsign_hashtable, 0, sizeof(callsign_hashtable_t) * CALLSIGN_HASHTABLE_MAX);

    ft8->padding0 = 0xdeadbeec;
    ft8->padding1 = 0xdeadbeed;
    ft8->padding2 = 0xdeadbeee;
}

static void hashtable_cleanup(int rx_chan, uint8_t max_age)
{
    decode_ft8_t *ft8 = &decode_ft8[rx_chan];
    callsign_hashtable_t *ht = ft8->callsign_hashtable;
    CHECK_PADDING(ft8);

    for (int idx_hash = 0; idx_hash < CALLSIGN_HASHTABLE_MAX; ++idx_hash, ht++)
    {
        if (ht->callsign[0] != '\0')
        {
            uint8_t age = (uint8_t)(ht->hash >> 22);
            if (age >= max_age)
            {
                LOG(LOG_INFO, "Removing [%.11s] from hash table, age = %d, uploaded = %d\n",
                    ht->callsign, age, ht->uploaded);
                //printf("FT8 hashtable REMOVE age=%d uploaded=%d %.11s\n", age, ht->uploaded, ht->callsign);
                // free the hash entry
                ht->callsign[0] = '\0';
                ht->uploaded = 0;
                ht->hash = 0;
                ft8->callsign_hashtable_size--;
            }
            else
            {
                //printf("FT8 hashtable AGE+1 age=%d uploaded=%d %.11s\n", age, ht->uploaded, ht->callsign);
                // increase callsign age
                ht->hash = (((uint32_t)age + 1u) << 22) | (ht->hash & 0x3FFFFFu);
            }
        }
    }
}

static void hashtable_add(const char* callsign, uint32_t hash)
{
    decode_ft8_t *ft8 = (decode_ft8_t *) &decode_ft8[FROM_VOID_PARAM(TaskGetUserParam())];
    uint16_t hash10 = (hash >> 12) & 0x3FFu;
    int idx_hash = (hash10 * 23) % CALLSIGN_HASHTABLE_MAX;
    callsign_hashtable_t *ht = &ft8->callsign_hashtable[idx_hash];
    int wrap_idx = -1;

    while (ht->callsign[0] != '\0')
    {
        if (((ht->hash & 0x3FFFFFu) == hash) && (0 == strncmp(ht->callsign, callsign, 11)))
        {
            #ifdef PR_USE_CALLSIGN_HASHTABLE
                // Don't reset age when using callsign hashtable as PSKReporter upload limiter.
            #else
                // reset age
                ht->hash &= 0x3FFFFFu;
            #endif

            LOG(LOG_DEBUG, "Found a duplicate [%s]\n", callsign);
            //printf("hashtable_add DUP idx=%d hash=0x%x <%s>\n", idx_hash, hash, callsign);
            return;
        }
        else
        {
            if (wrap_idx == -1) wrap_idx = idx_hash;
            LOG(LOG_DEBUG, "Hash table clash!\n");
            // Move on to check the next entry in hash table
            idx_hash = (idx_hash + 1) % CALLSIGN_HASHTABLE_MAX;
            ht = &ft8->callsign_hashtable[idx_hash];
            if (idx_hash == wrap_idx) {
                ft8->callsign_hashtable_size--;
                //printf("hashtable_add WRAPPED idx=%d hash=0x%x <%s>\n", idx_hash, hash, callsign);
                break;
            }
        }
    }
    ft8->callsign_hashtable_size++;
    strncpy(ht->callsign, callsign, 11);    // NB: strncpy does zero-fill
    ht->hash = hash;
}

static callsign_hashtable_t *hashtable_find(const char *callsign)
{
    decode_ft8_t *ft8 = (decode_ft8_t *) &decode_ft8[FROM_VOID_PARAM(TaskGetUserParam())];

    for (int i = 0; i < CALLSIGN_HASHTABLE_MAX; i++) {
        callsign_hashtable_t *ht = &ft8->callsign_hashtable[i];
        if (ht->callsign[0] != '\0' && strncmp(ht->callsign, callsign, sizeof(ht->callsign)) == 0) {
            return ht;
        }
    }

    return NULL;
}

static bool hashtable_lookup(ftx_callsign_hash_type_t hash_type, uint32_t hash, char* callsign)
{
    decode_ft8_t *ft8 = (decode_ft8_t *) &decode_ft8[FROM_VOID_PARAM(TaskGetUserParam())];
    uint8_t hash_shift = (hash_type == FTX_CALLSIGN_HASH_10_BITS) ? 12 : (hash_type == FTX_CALLSIGN_HASH_12_BITS ? 10 : 0);
    uint16_t hash10 = (hash >> (12 - hash_shift)) & 0x3FFu;
    int idx_hash = (hash10 * 23) % CALLSIGN_HASHTABLE_MAX;
    callsign_hashtable_t *ht = &ft8->callsign_hashtable[idx_hash];
    int wrap_idx = -1;

    while (ht->callsign[0] != '\0')
    {
        if (((ht->hash & 0x3FFFFFu) >> hash_shift) == hash)
        {
            strncpy(callsign, ht->callsign, 11);    // NB: strncpy does zero-fill
            return true;
        }
        // Move on to check the next entry in hash table until entire table has been searched.
        // Unless an empty entry is hit which means a match cannot exist
        if (wrap_idx == -1) wrap_idx = idx_hash;
        idx_hash = (idx_hash + 1) % CALLSIGN_HASHTABLE_MAX;
        ht = &ft8->callsign_hashtable[idx_hash];
        if (idx_hash == wrap_idx) {
            //printf("hashtable_lookup WRAPPED hash=0x%x\n", hash);
            break;
        }
    }
    callsign[0] = '\0';
    return false;
}

ftx_callsign_hash_interface_t hash_if = {
    .lookup_hash = hashtable_lookup,
    .save_hash = hashtable_add
};

static void decode(int rx_chan, const frame_ft8_t* frame, int freqHz)
{
    decode_ft8_t *ft8 = &decode_ft8[rx_chan];
    const monitor_t* mon = &frame->mon;

    CHECK_PADDING(ft8);

    const ftx_waterfall_t* wf = &mon->wf;

    CHECK_PADDING(ft8);

    // Hash table for decoded messages (to check for duplicates)
    int num_decoded = 0, num_spots = 0;

    // Initialize hash table pointers
    for (int i = 0; i < kMax_decoded_messages; ++i)
    {
        ft8->decoded_hashtable[i] = NULL;
    }

    // Go over candidates and attempt to decode messages
    bool need_header = true;
    int limiter = 0;

    for(int pass = 0; pass < 2; ++pass)
    {
    // Find top candidates by Costas sync score and localize them in time and frequency
    int num_candidates = ftx_find_candidates(wf, kMax_candidates / (pass + 1), ft8->candidate_list, kMin_score);

    for (int idx = 0; idx < num_candidates; ++idx)
    {
        const ftx_candidate_t* cand = &ft8->candidate_list[idx];

        float freq_hz = (mon->min_bin + cand->freq_offset + (float)cand->freq_sub / wf->freq_osr) / mon->symbol_period;
        float time_sec = (cand->time_offset + (float)cand->time_sub / wf->time_osr) * mon->symbol_period;

#ifdef WATERFALL_USE_PHASE
        // int resynth_len = 12000 * 16;
        // float resynth_signal[resynth_len];
        // for (int pos = 0; pos < resynth_len; ++pos)
        // {
        //     resynth_signal[pos] = 0;
        // }
        // monitor_resynth(mon, cand, resynth_signal);
        // char resynth_path[80];
        // sprintf(resynth_path, "resynth_%04f_%02.1f.wav", freq_hz, time_sec);
        // save_wav(resynth_signal, resynth_len, 12000, resynth_path);
#endif

        ftx_message_t message;
        ftx_decode_status_t status;
        if (!ftx_decode_candidate(wf, cand, kLDPC_iterations, &message, &status))
        {
            if (status.ldpc_errors > 0)
            {
                LOG(LOG_DEBUG, "LDPC decode: %d errors\n", status.ldpc_errors);
            }
            else if (!status.crc_valid)
            {
                LOG(LOG_DEBUG, "CRC mismatch!\n");
            }
            continue;
        }
        CHECK_PADDING(ft8);

        LOG(LOG_DEBUG, "Checking hash table for %4.1fs / %4.1fHz [%d]...\n", time_sec, freq_hz, cand->score);
        int idx_hash = message.hash % kMax_decoded_messages;
        bool found_empty_slot = false;
        bool found_duplicate = false;

        do
        {
            if (ft8->decoded_hashtable[idx_hash] == NULL)
            {
                LOG(LOG_DEBUG, "Found an empty slot\n");
                found_empty_slot = true;
            }
            else if ((ft8->decoded_hashtable[idx_hash]->hash == message.hash) && (0 == memcmp(ft8->decoded_hashtable[idx_hash]->payload, message.payload, sizeof(message.payload))))
            {
                LOG(LOG_DEBUG, "Found a duplicate!\n");
                found_duplicate = true;
            }
            else
            {
                LOG(LOG_DEBUG, "Hash table clash!\n");
                // Move on to check the next entry in hash table
                idx_hash = (idx_hash + 1) % kMax_decoded_messages;
            }
        } while (!found_empty_slot && !found_duplicate);

    CHECK_PADDING(ft8);

        if (found_empty_slot)
        {
            char text[FTX_MAX_MESSAGE_LENGTH];
            bool uploaded = false;
            int km = 0;
            bool fst4w = ft8->protocol == FTX_PROTOCOL_FST4W;
            ftx_message_rc_t unpack_status = fst4w?
                fst4w_message_decode(&message, &hash_if, text) :
                ftx_message_decode(&message, &hash_if, text);
            if (unpack_status != FTX_MESSAGE_RC_OK && unpack_status != FTX_MESSAGE_RC_ERROR_TYPE)
            {
                snprintf(text, sizeof(text), "Error [%d] while unpacking!", (int)unpack_status);
                continue;
            }

            // Fill the empty hashtable slot
            memcpy(&ft8->decoded[idx_hash], &message, sizeof(message));
            ft8->decoded_hashtable[idx_hash] = &ft8->decoded[idx_hash];
            ++num_decoded;

            float snr = message.snr * 0.5f + ft8_conf.SNR_adj;
            bool pskr_ok = false;
            int age = 0;
            int tx_power = 0;
            char call_to[14], call_de[14], grid_de[7];
            call_to[0] = call_de[0] = grid_de[0] = '\0';
            if (fst4w) {
                if (sscanf(text, "%13s %6s %d", call_de, grid_de, &tx_power) == 3 &&
                    call_de[0] != '<' && strlen(call_de) >= 3 &&
                    (strlen(grid_de) == 4 || strlen(grid_de) == 6)) {
                    pskr_ok = true;
                }
            } else if (ftx_message_get_type(&message) == FTX_MESSAGE_TYPE_STANDARD &&
                ftx_message_decode_std(&message, &hash_if, call_to, call_de, grid_de) == FTX_MESSAGE_RC_OK &&
                call_de[0] != '<' && strlen(call_de) >= 3 && strlen(grid_de) == 4 && strcmp(grid_de, "RR73") != 0) {
                pskr_ok = true;
            }

            if (pskr_ok) {
                #ifdef PR_USE_CALLSIGN_HASHTABLE
                    callsign_hashtable_t *ht = hashtable_find(call_de);
                    if (ht != NULL) {
                        if (ht->uploaded == 0) {
                            if (limiter <= PR_LIMITER) {
                                #ifdef PR_TESTING
                                    limiter++;
                                #endif

                                u4_t passband_freq = (u4_t) roundf(freq_hz);
                                s1_t snr_i = (s1_t) roundf(snr);
                                #ifdef PR_TESTING
                                #else
                                    if (!ft8_conf.test)
                                #endif
                                    {
                                        km = PSKReporter_spot(rx_chan, call_de, passband_freq, snr_i,
                                            fst4w? "FST4W" : ((ft8->protocol == FTX_PROTOCOL_FT8)? "FT8" : "FT4"),
                                            grid_de, frame->decode_time, ft8->slot);
                                    }
                                ht->uploaded = 1;
                                uploaded = true;
                                num_spots++;
                            }
                        } else {
                            km = PSKReporter_distance(grid_de);
                            age = ht->hash >> 22;
                        }
                    }
                #endif
            }
    CHECK_PADDING(ft8);

            if (need_header) {
                ext_send_msg_encoded(rx_chan, false, "EXT", "chars",
                //   22:40:30 +11.0 +2.80 1931 nnnnn  nn i.n  CQ DH1NAS JO50
                    "     UTC   SNR    dT Freq    km age msg  freq: %.2f  mode: %s\n",
                    freqHz/1e3, ft8->protocol_s);
                need_header = false;
            }

            time_sec += ft8_conf.dT_adj;

            LOG(LOG_INFO, "%02d%02d%02d %+05.1f %+4.2f %4.0f %5d  %s\n",
                frame->tm_slot_start.tm_hour, frame->tm_slot_start.tm_min, frame->tm_slot_start.tm_sec,
                snr, time_sec, freq_hz, km, text);

            char *ks = NULL;
            ks = kstr_asprintf(ks, "%02d:%02d:%02d %+05.1f %+4.2f %4.0f",
                frame->tm_slot_start.tm_hour, frame->tm_slot_start.tm_min, frame->tm_slot_start.tm_sec,
                snr, time_sec, freq_hz);
            ks = kstr_asprintf(ks, (km > 0)? " %5d" : "      ", km);
            ks = kstr_asprintf(ks, (age != 0)? "  %02d" : "    ", age);

            if (fst4w) {
                ks = kstr_asprintf(ks, " WSPR ");
            } else {
                uint8_t i3 = ftx_message_get_i3(&message);
                if (i3)
                    ks = kstr_asprintf(ks, " %d.0 ", i3);
                else
                    ks = kstr_asprintf(ks, " 0.%d*", ftx_message_get_n3(&message));
            }

            if (pskr_ok) {
                char *call_to_s = NULL, *call_de_s, *grid_de_s;

                // silently ignore incorrect encoding of RR73 (i.e. sent as grid code instead of MAXGRID4+3)
                bool rr73 = (strcmp(grid_de, "RR73") == 0);

                // call_to
                if (fst4w) {
                    call_to_s = strdup("");
                } else if (strcmp(call_to, "&lt;...&gt;") != 0 &&
                    strcmp(call_to, "CQ") != 0 &&
                    strncmp(call_to, "CQ ", 3) != 0 &&
                    strcmp(call_to, "DE") != 0 &&
                    strcmp(call_to, "QRZ") != 0)
                    asprintf(&call_to_s, "<a style=\"color:blue\" href=\"https://www.qrz.com/lookup/%s\" target=\"_blank\">%s</a>", call_to, call_to);
                else {
                    call_to_s = strdup(call_to);
                }

                // call_de
                asprintf(&call_de_s, "<a style=\"color:blue\" href=\"https://www.qrz.com/lookup/%s\" target=\"_blank\">%s</a>", call_de, call_de);
                asprintf(&grid_de_s, rr73? "(RR73)" : "<a style=\"color:blue\" href=\"http://www.levinecentral.com/ham/grid_square.php?"
                    "Grid=%s\" target=\"_blank\">%s</a>", grid_de, grid_de);

                const char *protocol = fst4w? "FST4W" : ((ft8->protocol == FTX_PROTOCOL_FT8)? "FT8" : "FT4");
                conn_t *conn = rx_channels[rx_chan].conn;
                u4_t freq = conn->freqHz + ft8_conf.freq_offset_Hz + freq_hz;
                mqtt_publish(protocol, "\"call:\":\"%s\", \"call_to\":\"%s\", \"grid\":\"%s\", \"snr\":%.1f, \"dT\":%.2f, \"freq\":%.3f, \"km\":%d, \"age\":%d",
                    call_de, call_to, grid_de, snr, time_sec, (double) freq / 1e3, km, age);

                if (fst4w) {
                    ext_send_msg_encoded(rx_chan, false, "EXT", "chars",
                        "%s %s%s %s%s %d dBm\n", kstr_sp(ks), ft8->debug? "W> ":"",
                        uploaded? GREEN : "", call_de_s, grid_de_s, tx_power);
                } else {
                    ext_send_msg_encoded(rx_chan, false, "EXT", "chars",
                        "%s %s%s %s%s %s" NONL, kstr_sp(ks), ft8->debug? "3> ":"", call_to_s,
                        uploaded? GREEN : "", call_de_s, grid_de_s);
                }

                free(call_to_s); free(call_de_s); free(grid_de_s);
            } else {
                ext_send_msg_encoded(rx_chan, false, "EXT", "chars",
                    "%s %s%s\n", kstr_sp(ks), ft8->debug? "0> ":"", text);
            }

            kstr_free(ks);
        }
    }
    }
    CHECK_PADDING(ft8);

    LOG(LOG_INFO, "Decoded %d messages, callsign hashtable size %d\n", num_decoded, ft8->callsign_hashtable_size);
    if (num_decoded > 0) {
        char *ks = NULL;
        ks = kstr_asprintf(ks, "%02d:%02d:%02d %s decoded %d, ",
            frame->tm_slot_start.tm_hour, frame->tm_slot_start.tm_min, frame->tm_slot_start.tm_sec,
            ft8->protocol_s, num_decoded);
        if (num_spots != 0) {
            ks = kstr_asprintf(ks, "new spots %d, ", num_spots);
        }

        static u4_t last_num_uploads[MAX_RX_CHANS];
        u4_t num_uploads = PSKReporter_num_uploads(rx_chan);
        if (num_uploads > last_num_uploads[rx_chan]) {
            u4_t diff = num_uploads - last_num_uploads[rx_chan];
            //printf("FT8 SPOTS %+d num_uploads=%d last_num_uploads=%d\n", diff, num_uploads, last_num_uploads[rx_chan]);
            last_num_uploads[rx_chan] = num_uploads;
            if (diff != 0) {
                ks = kstr_asprintf(ks, YELLOW "uploaded %d spot%s to pskreporter.info" NORM ", ",
                    diff, (diff == 1)? "" : "s");
            }
        } else
        if (num_uploads < last_num_uploads[rx_chan]) {
            //printf("FT8 RESET num_uploads=%d last_num_uploads=%d\n", num_uploads, last_num_uploads[rx_chan]);
            last_num_uploads[rx_chan] = num_uploads;
        }

        ks = kstr_asprintf(ks, "hashtable %d%%",
            ft8->callsign_hashtable_size * 100 / CALLSIGN_HASHTABLE_MAX);
        ext_send_msg_encoded(rx_chan, false, "EXT", "chars", "%s\n", kstr_sp(ks));
        kstr_free(ks);
    }

    if (frame->tm_slot_start.tm_sec == 0) {
        hashtable_cleanup(rx_chan, CALLSIGN_AGE_MAX);
    }
}

void decode_ft8_compute(void *arg) {
    decode_ft8_t *ft8 = (decode_ft8_t *)arg;
    TaskSetUserParam(TO_VOID_PARAM(ft8->rx_chan));

    while (ft8->init) {
        TaskSleep();

        CHECK_PADDING(ft8);

        if (ft8->init && ft8->frame_idx_decode != ft8->frame_idx_fill)
        {
            frame_ft8_t *frame = &ft8->frames[ft8->frame_idx_decode];

            LOG(LOG_DEBUG, "FT8 Waterfall accumulated %d symbols\n", frame->mon.wf.num_blocks);
            LOG(LOG_INFO, "FT8 Max magnitude: %.1f dB\n", frame->mon.max_mag);

            decode(ft8->rx_chan, frame, ft8->freqHz);

            CHECK_PADDING(ft8);

            // Streaming frames are reset when reused so shared FFT state is not
            // disturbed while the next frame is being captured.
            if (!ft8->streaming)
                monitor_reset(&frame->mon);

            CHECK_PADDING(ft8);

            ft8->frame_idx_decode = (ft8->frame_idx_decode + 1) % FRAME_SIZE;
        }
    }
}

void decode_ft8_setup(int rx_chan, int debug)
{
    decode_ft8_t *ft8 = &decode_ft8[rx_chan];
    TaskSetUserParam(TO_VOID_PARAM(rx_chan));
    ft8->debug = debug;
    int have_call_and_grid = PSKReporter_setup(rx_chan);
    if (have_call_and_grid != 0) ft8->have_call_and_grid = have_call_and_grid;
}

void decode_ft8_samples(int rx_chan, TYPEMONO16 *samps, int nsamps, int freqHz, u1_t *start_test)
{
    decode_ft8_t *ft8 = &decode_ft8[rx_chan];
    ft8->freqHz = freqHz;
    if (!ft8->init) return;

    frame_ft8_t *frame = &ft8->frames[ft8->frame_idx_fill];

    if (!ft8->tsync) {
        const float time_shift = 0.8;
        struct timespec spec;
        clock_gettime(CLOCK_REALTIME, &spec);
        double time_sec = (double) spec.tv_sec + (spec.tv_nsec / 1e9);
        double time_within_slot = fmod(time_sec - time_shift, ft8->slot_period);
        if (time_within_slot > ft8->slot_period / 4) {
            *start_test = 0;
            return;     // wait for beginning of slot
        }

        time_t time_slot_start = (time_t) (time_sec - time_within_slot);
        gmtime_r(&time_slot_start, &frame->tm_slot_start);
        LOG(LOG_INFO, "FT8 Time within slot %02d:%02d:%02d %.3f s\n", frame->tm_slot_start.tm_hour,
            frame->tm_slot_start.tm_min, frame->tm_slot_start.tm_sec, time_within_slot);
        ft8->in_pos = ft8->frame_pos = 0;
        *start_test = 1;
        frame->decode_time = spec.tv_sec;
        ft8->slot++;
        ft8->tsync = true;
    }
    CHECK_PADDING(ft8);

    if (ft8->streaming) {
        monitor_stream_process_i16(&ft8->mon_stream, samps, nsamps);
        if (frame->mon.wf.num_blocks < frame->mon.wf.max_blocks)
            return;

        *start_test = 0;
        ft8->frame_idx_fill = (ft8->frame_idx_fill + 1) % FRAME_SIZE;
        if (!monitor_stream_set_frame(&ft8->mon_stream, &ft8->frames[ft8->frame_idx_fill].mon)) {
            printf("FST4W: unable to reset streaming monitor on rx%d\n", rx_chan);
            ft8->init = false;
            return;
        }
        TaskWakeup(ft8->compute_task);
        ft8->tsync = false;
        return;
    }

    for (int i = 0; i < nsamps /*&& ft8->in_pos < ft8->num_samples*/; i++) {
        // NB: must normalize to +/- 1.0 or there won't be any decodes
        ft8->samples[ft8->in_pos] = ((TYPEREAL) samps[i]) / 32768.0f;
        ft8->in_pos++;
    }

    int block_size = frame->mon.block_size;
    if (ft8->in_pos < ft8->frame_pos + block_size)
        return;      // not yet a full block

    while (ft8->in_pos >= ft8->frame_pos + block_size && ft8->frame_pos < ft8->num_samples) {
        monitor_process(&frame->mon, ft8->samples + ft8->frame_pos);
        ft8->frame_pos += block_size;
    }
    if (ft8->frame_pos < ft8->num_samples)
        return;

    *start_test = 0;

    ft8->frame_idx_fill = (ft8->frame_idx_fill + 1) % FRAME_SIZE;

    TaskWakeup(ft8->compute_task);

    ft8->tsync = false;
}

void decode_ft8_init(int rx_chan, int proto)
{
    decode_ft8_t *ft8 = &decode_ft8[rx_chan];
    memset(ft8, 0, sizeof(decode_ft8_t));
    ft8->magic = 0xbeefcafe;
    ft8->rx_chan = rx_chan;
    ftx_protocol_t protocol = FTX_PROTOCOL_FT8;
    int tr_period = 0;
    if (proto == FT8_PROTOCOL_FT4) {
        protocol = FTX_PROTOCOL_FT4;
    } else if (proto >= FT8_PROTOCOL_FST4W_15 && proto <= FT8_PROTOCOL_FST4W_1800) {
        protocol = FTX_PROTOCOL_FST4W;
        tr_period = kFST4_TR_periods[proto - FT8_PROTOCOL_FST4W_15];
    }
    ft8->protocol = protocol;
    ft8->tr_period = tr_period;
    float slot_period = protocol == FTX_PROTOCOL_FT8? FT8_SLOT_TIME :
        (protocol == FTX_PROTOCOL_FT4? FT4_SLOT_TIME : (float) tr_period);
    if (protocol == FTX_PROTOCOL_FST4W)
        snprintf(ft8->protocol_s, sizeof(ft8->protocol_s), "FST4W-%d", tr_period);
    else
        snprintf(ft8->protocol_s, sizeof(ft8->protocol_s), "FT%d", protocol == FTX_PROTOCOL_FT8? 8:4);
    ft8->slot_period = slot_period;
    int sample_rate = protocol == FTX_PROTOCOL_FST4W? 6000 : snd_rate;

    // Compute FFT over the whole signal and store it
    monitor_config_t mon_cfg = {
        .f_min = protocol == FTX_PROTOCOL_FST4W? FST4W_PASSBAND_LO : FT8_PASSBAND_LO,
        .f_max = protocol == FTX_PROTOCOL_FST4W? FST4W_PASSBAND_HI : FT8_PASSBAND_HI,
        .sample_rate = sample_rate,
        .time_osr = kTime_osr,
        .freq_osr = kFreq_osr,
        .protocol = protocol,
        .tr_period = tr_period
    };

    hashtable_init(rx_chan);

    if (protocol == FTX_PROTOCOL_FST4W) {
        monitor_memory_usage_t memory;
        if (!monitor_get_memory_usage(&mon_cfg, &memory) ||
            !monitor_shared_init(&ft8->mon_shared, &mon_cfg)) {
            printf("FST4W: unable to allocate streaming monitor on rx%d\n", rx_chan);
            decode_ft8_free(rx_chan);
            return;
        }
        ft8->streaming = true;
        for (int i = 0; i < FRAME_SIZE; i++) {
            if (!monitor_frame_init(&ft8->frames[i].mon, &ft8->mon_shared)) {
                printf("FST4W: unable to allocate waterfall frame on rx%d\n", rx_chan);
                decode_ft8_free(rx_chan);
                return;
            }
        }
        if (!monitor_stream_init(&ft8->mon_stream, &ft8->frames[0].mon, snd_rate)) {
            printf("FST4W: unable to initialize streaming input on rx%d\n", rx_chan);
            decode_ft8_free(rx_chan);
            return;
        }
        LOG(LOG_INFO, "FST4W-%d streaming memory shared=%zu frame=%zu\n",
            tr_period, memory.shared_bytes, memory.frame_bytes);
    } else {
        int num_samples = slot_period * sample_rate;
        ft8->samples = (TYPEREAL *) malloc(num_samples * sizeof(TYPEREAL));
        if (ft8->samples == NULL) {
            printf("FT8: unable to allocate sample buffer on rx%d\n", rx_chan);
            decode_ft8_free(rx_chan);
            return;
        }
        ft8->num_samples = (slot_period - 0.4f) * sample_rate;
        for (int i = 0; i < FRAME_SIZE; i++) {
            if (!monitor_init(&ft8->frames[i].mon, &mon_cfg)) {
                printf("FT8: unable to allocate monitor on rx%d\n", rx_chan);
                decode_ft8_free(rx_chan);
                return;
            }
        }
    }

    ft8->frame_idx_decode = ft8->frame_idx_fill = 0;

    LOG(LOG_DEBUG, "FT8 Waterfall allocated %d symbols\n", ft8->frames[0].mon.wf.max_blocks);
    ft8->tsync = false;
    PSKReporter_reset(rx_chan);
    ft8->init = true;

    ft8->compute_task = CreateTask(decode_ft8_compute, TO_VOID_PARAM(ft8), LOWEST_PRIORITY);
    LOG(LOG_DEBUG, "Compute task id: %d\n", ft8->compute_task);
}

void decode_ft8_free(int rx_chan)
{
    decode_ft8_t *ft8 = &decode_ft8[rx_chan];
    ft8->init = false;
    free(ft8->samples);
    ft8->samples = NULL;

    if (ft8->compute_task)
        TaskRemove(ft8->compute_task);

    free(ft8->callsign_hashtable);
    ft8->callsign_hashtable = NULL;

    if (ft8->streaming)
        monitor_stream_free(&ft8->mon_stream);
    for(int i = 0 ; i < FRAME_SIZE; i++)
        monitor_free(&ft8->frames[i].mon);
    if (ft8->streaming)
        monitor_shared_free(&ft8->mon_shared);
}

void decode_ft8_protocol(int rx_chan, int freqHz, int proto)
{
    decode_ft8_t *ft8 = &decode_ft8[rx_chan];
    decode_ft8_free(rx_chan);
    decode_ft8_init(rx_chan, proto);
    ext_send_msg_encoded(rx_chan, false, "EXT", "chars",
        "-------------------------------------------------------  new freq %.2f mode %s\n",
        freqHz/1e3, ft8->protocol_s);
}
