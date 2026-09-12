#include "types.h"
#include "config.h"
#include "clk.h"
#include "peri.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <unistd.h>

namespace {

const int kFakeRxChannels = 4;
const int kFakeWaterfallChannels = 4;
const double kTwoPi = 6.28318530717958647692;

std::atomic<u64_t> rx_sample_index(0);
std::atomic<u64_t> wf_sample_index[MAX_WF_CHANS];
std::atomic<int> wf_owner[MAX_WF_CHANS];

}

void peri_init()
{
    printf("native harness: fake Zynq hardware enabled\n");
}

void peri_free()
{
}

void rf_enable_airband(bool enabled)
{
    (void) enabled;
}

void rf_attn_set(float attn_dB)
{
    (void) attn_dB;
}

void sd_enable(bool write)
{
    (void) write;
}

void clock_correction(float freq_error)
{
    (void) freq_error;
}

void clock_reset_correction()
{
}

u64_t fpga_dna()
{
    return 0x8880000000000001ULL;
}

void fpga_start_rx()
{
    rx_sample_index.store(0);
}

void fpga_rxfreq(int rx_chan, uint64_t freq)
{
    (void) rx_chan;
    (void) freq;
}

void fpga_read_rx(void* buf, uint32_t size)
{
    s4_t* samples = static_cast<s4_t*>(buf);
    const int frame_size = 2 * rx_chans;
    const int frames = size / (sizeof(s4_t) * frame_size);
    const u64_t start = rx_sample_index.fetch_add(frames);
    const double amplitude = 0.18 * 2147483647.0;

    for (int frame = 0; frame < frames; frame++) {
        for (int ch = 0; ch < rx_chans; ch++) {
            const double tone_hz = 700.0 + ch * 230.0;
            const double phase = kTwoPi * tone_hz * (start + frame) / snd_rate;
            samples[frame * frame_size + ch * 2] = static_cast<s4_t>(amplitude * std::cos(phase));
            samples[frame * frame_size + ch * 2 + 1] = static_cast<s4_t>(amplitude * std::sin(phase));
        }
    }

    const useconds_t delay = static_cast<useconds_t>(
        (static_cast<u64_t>(frames) * 1000000ULL) / snd_rate);
    usleep(delay > 0 ? delay : 1);
}

void fpga_start_pps()
{
}

uint64_t fpga_read_pps()
{
    return static_cast<uint64_t>(ADC_CLOCK_TYP);
}

int fpga_set_antenna(int mask)
{
    (void) mask;
    return 0;
}

int fpga_set_pga(bool enabled)
{
    (void) enabled;
    return 0;
}

int fpga_set_dither(bool enabled)
{
    (void) enabled;
    return 0;
}

int fpga_set_led(bool enabled)
{
    (void) enabled;
    return 0;
}

uint32_t fpga_signature()
{
    return kFakeRxChannels | (kFakeWaterfallChannels << 8);
}

void fpga_setovmask(uint32_t mask)
{
    (void) mask;
}

void fpga_setadclvl(uint32_t val)
{
    (void) val;
}

int fpga_reset_wf(int wf_chan, bool cont)
{
    (void) cont;
    wf_sample_index[wf_chan].store(0);
    return 0;
}

int fpga_wf_param(int wf_chan, int decimate, uint64_t freq)
{
    (void) wf_chan;
    (void) decimate;
    (void) freq;
    return 0;
}

int fpga_get_wf(int rx_chan)
{
    for (;;) {
        for (int wf_chan = 0; wf_chan < kFakeWaterfallChannels; wf_chan++) {
            int expected = 0;
            if (wf_owner[wf_chan].compare_exchange_strong(expected, rx_chan + 1))
                return wf_chan;
        }
        usleep(1000);
    }
}

void fpga_free_wf(int wf_chan, int rx_chan)
{
    (void) rx_chan;
    wf_owner[wf_chan].store(0);
}

void fpga_read_wf(int wf_chan, void* buf, uint32_t size)
{
    s2_t* samples = static_cast<s2_t*>(buf);
    const int frames = size / (sizeof(s2_t) * 2);
    const u64_t start = wf_sample_index[wf_chan].fetch_add(frames);

    for (int frame = 0; frame < frames; frame++) {
        const double n = static_cast<double>(start + frame);
        double i = 4500.0 * std::cos(kTwoPi * 0.037 * n);
        double q = 4500.0 * std::sin(kTwoPi * 0.037 * n);
        i += 2800.0 * std::cos(kTwoPi * 0.113 * n);
        q += 2800.0 * std::sin(kTwoPi * 0.113 * n);
        i += 1400.0 * std::cos(kTwoPi * 0.241 * n);
        q += 1400.0 * std::sin(kTwoPi * 0.241 * n);
        samples[frame * 2] = static_cast<s2_t>(i);
        samples[frame * 2 + 1] = static_cast<s2_t>(q);
    }
}
