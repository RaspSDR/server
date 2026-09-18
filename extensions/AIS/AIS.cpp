// Copyright (c) 2026 RaspSDR contributors

#include "AIS.h"
#include "data_pump.h"
#include "ext.h"
#include "mem.h"

#include <stdio.h>
#include <string.h>

#define AIS_BAUD 9600
#define AIS_MAX_FRAME_BYTES (AIS_MAX_FRAME_BITS / 8)

static ais_chan_t ais_chan[MAX_RX_CHANS];

static u2_t ais_crc16(const u1_t *data, int length)
{
    u2_t crc = 0xffff;
    for (int i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 1)? (crc >> 1) ^ 0x8408 : crc >> 1;
    }
    return crc ^ 0xffff;
}

static u4_t ais_bits(const u1_t *data, int start, int length)
{
    u4_t value = 0;
    for (int i = 0; i < length; i++)
        value = (value << 1) | ((data[(start + i) / 8] >> (7 - ((start + i) % 8))) & 1);
    return value;
}

static s4_t ais_signed_bits(const u1_t *data, int start, int length)
{
    u4_t value = ais_bits(data, start, length);
    return (value & (1U << (length - 1)))? (s4_t) (value - (1U << length)) : (s4_t) value;
}

static void ais_string(const u1_t *data, int start, int length, char *out, int out_length)
{
    int count = length / 6;
    if (count >= out_length) count = out_length - 1;
    for (int i = 0; i < count; i++) {
        int value = ais_bits(data, start + i * 6, 6);
        char c = (char) (value < 32? value + 64 : value);
        out[i] = (c == '@')? ' ' : ((c >= 32 && c <= 126)? c : ' ');
    }
    out[count] = '\0';
    while (count > 0 && out[count - 1] == ' ') out[--count] = '\0';
}

static void ais_payload(const u1_t *data, int length, char *out, int out_length)
{
    int bit_count = length * 8;
    int pos = 0;
    for (int start = 0; start < bit_count && pos + 1 < out_length; start += 6) {
        int value = 0;
        for (int i = 0; i < 6; i++)
            value = (value << 1) | (start + i < bit_count? ais_bits(data, start + i, 1) : 0);
        out[pos++] = (char) (value < 40? value + 48 : value + 56);
    }
    out[pos] = '\0';
}

static void ais_report(int rx_chan, const u1_t *data, int length)
{
    if (length < 5) return;

    int type = ais_bits(data, 0, 6);
    unsigned mmsi = ais_bits(data, 8, 30);
    int has_position = 0, lon = 0, lat = 0, speed = -1, course = -1, heading = -1;
    char name[21] = "", callsign[8] = "";

    if (type >= 1 && type <= 3 && length >= 21) {
        speed = ais_bits(data, 50, 10);
        lon = ais_signed_bits(data, 61, 28);
        lat = ais_signed_bits(data, 89, 27);
        course = ais_bits(data, 116, 12);
        heading = ais_bits(data, 128, 9);
        has_position = lon != 108600000 && lat != 54600000;
    } else if ((type == 18 || type == 19) && length >= 21) {
        speed = ais_bits(data, 46, 10);
        lon = ais_signed_bits(data, 57, 28);
        lat = ais_signed_bits(data, 85, 27);
        course = ais_bits(data, 112, 12);
        heading = ais_bits(data, 124, 9);
        has_position = lon != 108600000 && lat != 54600000;
        if (type == 19 && length >= 33) ais_string(data, 143, 120, name, sizeof(name));
    } else if (type == 5 && length >= 35) {
        ais_string(data, 70, 42, callsign, sizeof(callsign));
        ais_string(data, 112, 120, name, sizeof(name));
    } else if (type == 24 && length >= 20 && ais_bits(data, 38, 2) == 0) {
        ais_string(data, 40, 120, name, sizeof(name));
    }

    char encoded[80];
    ais_payload(data, length, encoded, sizeof(encoded));
    ext_send_msg_encoded(rx_chan, false, "EXT", "report",
        "type=%d|mmsi=%u|lat=%s%.6f|lon=%s%.6f|sog=%s%.1f|cog=%s%.1f|hdg=%s%d|name=%s|call=%s|payload=%s",
        type, mmsi,
        has_position? "" : "na:", has_position? lat / 600000.0 : 0.0,
        has_position? "" : "na:", has_position? lon / 600000.0 : 0.0,
        speed < 1023? "" : "na:", speed < 1023? speed / 10.0 : 0.0,
        course < 3600? "" : "na:", course < 3600? course / 10.0 : 0.0,
        heading < 511? "" : "na:", heading < 511? heading : 0,
        name, callsign, encoded);
}

static void ais_finish_frame(int rx_chan, ais_decoder_t *d)
{
    u1_t bytes[AIS_MAX_FRAME_BYTES];
    int bit_count = 0, value = 0, ones = 0;

    for (int i = 0; i < d->raw_count; i++) {
        int bit = d->raw[i];
        if (bit) {
            if (++ones > 5) return;
        } else if (ones == 5) {
            ones = 0;
            continue;
        } else {
            ones = 0;
        }

        value |= bit << (bit_count % 8);
        if ((++bit_count % 8) == 0) {
            if (bit_count / 8 > AIS_MAX_FRAME_BYTES) return;
            bytes[bit_count / 8 - 1] = value;
            value = 0;
        }
    }

    int length = bit_count / 8;
    if (length < 5) return;
    u2_t crc = ais_crc16(bytes, length - 2);
    if (bytes[length - 2] != (crc & 0xff) || bytes[length - 1] != (crc >> 8)) return;
    ais_report(rx_chan, bytes, length - 2);
}

static void ais_decoder_bit(int rx_chan, ais_decoder_t *d, int level)
{
    if (!d->have_level) {
        d->previous_level = level;
        d->have_level = true;
        return;
    }

    int bit = level == d->previous_level;
    d->previous_level = level;
    d->shift = ((d->shift << 1) | bit) & 0xff;
    if (d->in_frame && d->raw_count < AIS_MAX_FRAME_BITS)
        d->raw[d->raw_count++] = bit;

    if (d->shift == 0x7e) {
        if (d->in_frame && d->raw_count >= 8) {
            d->raw_count -= 8;
            ais_finish_frame(rx_chan, d);
        }
        d->in_frame = true;
        d->raw_count = 0;
    } else if (d->raw_count == AIS_MAX_FRAME_BITS) {
        d->in_frame = false;
    }
}

static void ais_decoder_sample(int rx_chan, ais_decoder_t *d, int sample)
{
    d->dc += ((float) sample - d->dc) * 0.001f;
    d->sum += sample - (int) d->dc;
    d->phase += AIS_BAUD;
    if (d->phase >= snd_rate) {
        d->phase -= snd_rate;
        ais_decoder_bit(rx_chan, d, d->sum >= 0);
        d->sum = 0;
    }
}

static void ais_task(void *param)
{
    while (1) {
        int rx_chan = (int) FROM_VOID_PARAM(TaskSleepReason("wait for AIS samples"));
        ais_chan_t *e = &ais_chan[rx_chan];
        rx_dpump_t *rx = &rx_dpump[rx_chan];

        while ((u4_t) e->rd_pos != rx->real_wr_pos) {
            if (rx->real_seqnum[e->rd_pos] != e->seq) {
                if (e->seq_init)
                    rcprintf(rx_chan, "AIS SEQ: @%d got %u expecting %u\n",
                        e->rd_pos, rx->real_seqnum[e->rd_pos], e->seq);
                else
                    e->seq_init = true;
                e->seq = rx->real_seqnum[e->rd_pos];
            }
            e->seq++;

            TYPEMONO16 *samples = &rx->real_samples_s2[e->rd_pos][0];
            for (int i = 0; i < FASTFIR_OUTBUF_SIZE; i++) {
                ais_decoder_sample(rx_chan, &e->decoder[0], samples[i]);
                ais_decoder_sample(rx_chan, &e->decoder[1], -samples[i]);
            }
            e->rd_pos = (e->rd_pos + 1) & (N_DPBUF - 1);
        }
    }
}

static void ais_close(int rx_chan)
{
    ais_chan_t *e = &ais_chan[rx_chan];
    e->running = false;
    ext_unregister_receive_real_samps_task(rx_chan);
    ext_unregister_receive_cmds(rx_chan);
    if (e->tid) TaskRemove(e->tid);
    memset(e, 0, sizeof(*e));
}

static bool ais_receive_cmds(u2_t key, char *cmd, int rx_chan)
{
    if (key != CMD_TUNE) return false;
    char *mode;
    double lo, hi, freq;
    int param;
    int n = sscanf(cmd, "SET mod=%16ms low_cut=%lf high_cut=%lf freq=%lf param=%d",
        &mode, &lo, &hi, &freq, &param);
    if (n != 4 && n != 5) return false;
    ais_chan[rx_chan].tuned_f = freq;
    ext_send_msg(rx_chan, false, "EXT freq=%.3f", freq / 1000.0);
    kiwi_asfree(mode);
    return true;
}

static bool ais_msgs(char *msg, int rx_chan)
{
    ais_chan_t *e = &ais_chan[rx_chan];
    if (strcmp(msg, "SET ext_server_init") == 0) {
        memset(e, 0, sizeof(*e));
        e->rx_chan = rx_chan;
        ext_send_msg(rx_chan, false, "EXT ready");
        return true;
    }

    if (strcmp(msg, "SET start") == 0) {
        if (e->running) return true;
        e->running = true;
        e->seq_init = false;
        e->tid = CreateTaskF(ais_task, TO_VOID_PARAM(rx_chan),
            EXT_PRIORITY, CTF_RX_CHANNEL | (rx_chan & CTF_CHANNEL));
        ext_register_receive_real_samps_task(e->tid, rx_chan);
        ext_register_receive_cmds(ais_receive_cmds, rx_chan);
        ext_send_msg(rx_chan, false, "EXT decoder=live");
        return true;
    }

    if (strcmp(msg, "SET stop") == 0) {
        ais_close(rx_chan);
        return true;
    }
    return false;
}

void AIS_main();
static ext_t ais_ext = { "AIS", AIS_main, ais_close, ais_msgs, EXT_NEW_VERSION, EXT_FLAGS_HEAVY };

void AIS_main()
{
#ifdef AIS
    ext_register(&ais_ext);
#else
    printf("ext_register: \"AIS\" not configured\n");
#endif
}
