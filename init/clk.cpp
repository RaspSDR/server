/*
--------------------------------------------------------------------------------
This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.
This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.
You should have received a copy of the GNU Library General Public
License along with this library; if not, write to the
Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
Boston, MA  02110-1301, USA.
--------------------------------------------------------------------------------
*/

// Copyright (c) 2017 John Seamons, ZL4VO/KF6VO

#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "clk.h"
#include "gps_.h"
#include "timing.h"
#include "web.h"
#include "misc.h"
#include "peri.h"
#include "eeprom.h"

#include <time.h>

#define CLK_PRINTF
#ifdef CLK_PRINTF
#define clk_printf(fmt, ...) \
    if (clk_printfs) printf(fmt, ##__VA_ARGS__)
#else
#define clk_printf(fmt, ...)
#endif

clk_t clk;

static int outside_window;
static bool clk_printfs;

double adc_clock_hz;

static const airband_clock_profile_t airband_clock_profiles[AIRBAND_ADC_CLOCK_COUNT] = {
    {
        "98.304 MHz",
        98304000,
        786432000,
        8,
        AIRBAND_RATE_12K | AIRBAND_RATE_24K,
        98304000,
        147456000
    },
    {
        "110.592 MHz",
        110592000,
        663552000,
        6,
        AIRBAND_RATE_12K | AIRBAND_RATE_24K | AIRBAND_RATE_36K,
        110592000,
        165888000
    }
};

static_assert(98304000U % (12000U * 256U) == 0, "98.304 MHz must support 12 kHz audio");
static_assert(98304000U % (24000U * 256U) == 0, "98.304 MHz must support 24 kHz audio");
static_assert(110592000U % (12000U * 256U) == 0, "110.592 MHz must support 12 kHz audio");
static_assert(110592000U % (24000U * 256U) == 0, "110.592 MHz must support 24 kHz audio");
static_assert(110592000U % (36000U * 256U) == 0, "110.592 MHz must support 36 kHz audio");
static_assert(786432000U == 98304000U * 8U, "98.304 MHz integer multisynth");
static_assert(663552000U == 110592000U * 6U, "110.592 MHz integer multisynth");

const airband_clock_profile_t* airband_clock_profile(int profile) {
    if (profile < 0 || profile >= AIRBAND_ADC_CLOCK_COUNT)
        return NULL;
    return &airband_clock_profiles[profile];
}

bool airband_clock_supports_rate(int profile, int snd_rate_index) {
    const airband_clock_profile_t* p = airband_clock_profile(profile);
    if (p == NULL || snd_rate_index < 0 || snd_rate_index > 2)
        return false;
    return (p->audio_rate_mask & (1U << snd_rate_index)) != 0;
}

int airband_clock_effective_profile(int requested_profile, int snd_rate_index) {
    if (airband_clock_supports_rate(requested_profile, snd_rate_index))
        return requested_profile;
    if (requested_profile == AIRBAND_ADC_CLOCK_98_304 && snd_rate_index == 2)
        return AIRBAND_ADC_CLOCK_110_592;
    return -1;
}

bool airband_clock_migrate_offset(double current_offset_kHz, u4_t target_adc_hz,
    double* migrated_offset_kHz) {
    static const double known_airband_offsets_kHz[] = {
        98304.0,
        100761.6,
        110592.0
    };
    const double migration_limit_kHz = 100.0;
    double closest_delta_kHz = migration_limit_kHz + 1;

    for (unsigned i = 0; i < ARRAY_LEN(known_airband_offsets_kHz); i++) {
        double delta_kHz = current_offset_kHz - known_airband_offsets_kHz[i];
        if (fabs(delta_kHz) < fabs(closest_delta_kHz))
            closest_delta_kHz = delta_kHz;
    }

    // The old UI rounded 100.7616 MHz to 100762.0 kHz.
    if (current_offset_kHz == 100762.0)
        closest_delta_kHz = 0;

    if (fabs(closest_delta_kHz) > migration_limit_kHz)
        return false;

    *migrated_offset_kHz = target_adc_hz / kHz + closest_delta_kHz;
    return true;
}

#ifdef NATIVE_HARNESS
static void airband_clock_self_test() {
    assert(airband_clock_effective_profile(AIRBAND_ADC_CLOCK_98_304, 0) ==
        AIRBAND_ADC_CLOCK_98_304);
    assert(airband_clock_effective_profile(AIRBAND_ADC_CLOCK_98_304, 1) ==
        AIRBAND_ADC_CLOCK_98_304);
    assert(airband_clock_effective_profile(AIRBAND_ADC_CLOCK_98_304, 2) ==
        AIRBAND_ADC_CLOCK_110_592);
    assert(airband_clock_effective_profile(AIRBAND_ADC_CLOCK_110_592, 2) ==
        AIRBAND_ADC_CLOCK_110_592);
    assert(airband_clock_effective_profile(-1, 0) == -1);

    double migrated;
    assert(airband_clock_migrate_offset(100761.6, 98304000, &migrated));
    assert(migrated == 98304.0);
    assert(airband_clock_migrate_offset(100762.0, 110592000, &migrated));
    assert(migrated == 110592.0);
    assert(airband_clock_migrate_offset(110600.5, 98304000, &migrated));
    assert(migrated == 98312.5);
    assert(!airband_clock_migrate_offset(116000.0, 98304000, &migrated));
}
#endif

u4_t adc_clock_nominal_hz() {
    return (u4_t) adc_clock_hz;
}

void clock_init() {
    bool err; // NB: all CFG_OPTIONAL because don't get defaulted early enough

#ifdef NATIVE_HARNESS
    airband_clock_self_test();
#endif

    if (kiwi.airband) {
        clk.airband_profile_requested = kiwi.airband_adc_clock;
        clk.airband_profile_effective =
            airband_clock_effective_profile(clk.airband_profile_requested, kiwi.snd_rate);
        if (clk.airband_profile_effective < 0) {
            lprintf("airband: invalid ADC clock profile=%d audio_rate=%d\n",
                clk.airband_profile_requested, 12000 * (1 + kiwi.snd_rate));
            panic("airband ADC clock configuration");
        }

        const airband_clock_profile_t* profile =
            airband_clock_profile(clk.airband_profile_effective);
        adc_clock_hz = profile->adc_hz;
        clk.airband_profile_forced =
            clk.airband_profile_requested != clk.airband_profile_effective;
        lprintf("airband: requested clock %s, effective clock %s%s\n",
            airband_clock_profile(clk.airband_profile_requested)->name,
            profile->name,
            clk.airband_profile_forced ? " (audio-rate override)" : "");
    }
    else {
        clk.airband_profile_requested = -1;
        clk.airband_profile_effective = -1;
        clk.airband_profile_forced = false;
        adc_clock_hz = ADC_CLOCK_HF;
        if (kiwi.narrowband)
            adc_clock_hz /= 2;
    }

    clk.do_corrections = cfg_int("ADC_clk2_corr", &err, CFG_OPTIONAL);
    if (err) clk.do_corrections = ADC_CLK_CORR_CONTINUOUS;
    clk.ext_ADC_clk = cfg_bool("ext_ADC_clk", &err, CFG_OPTIONAL);
    if (err) clk.ext_ADC_clk = false;
    clk.gpsdo_ext_clk = cfg_int("ext_ADC_freq", &err, CFG_OPTIONAL);
    if (err) clk.gpsdo_ext_clk = 0; // external is always 10Mhz

    if (clk.ext_ADC_clk)
        clk.clock_ref = 10 * MHz;
    else
        clk.clock_ref = eeprom_refclock();

    clk.adc_clock_base = ADC_CLOCK_TYP;
    printf("ADC_CLOCK: %.6f MHz %s\n",
           clk.adc_clock_base / MHz, clk.ext_ADC_clk ? "(ext clk connector)" : "");

    clk.manual_adj = 0;
    clk_printfs = kiwi_file_exists(DIR_CFG "/clk.debug");
}

void clock_conn_init(conn_t* conn) {
    conn->adc_clock_corrected = clk.adc_clock_base;
    conn->manual_offset = 0;
    conn->adjust_clock = true;
}

double adc_clock_system() {
    double new_clk = clk.adc_clock_base + clk.manual_adj;

    // apply effect of any manual clock corrections
    if (clk.do_corrections == ADC_CLK_CORR_DISABLED ||
        (clk.do_corrections >= ADC_CLK_CORR_CONTINUOUS && clk.adc_gps_clk_corrections == 0)) {
        for (conn_t* c = conns; c < &conns[N_CONNS]; c++) {
            if (!c->valid) continue;

            // adc_clock_corrected is what the sound and waterfall channels use for their NCOs
            c->adc_clock_corrected = new_clk;
        }

        if (clk.adc_clk_corrections != clk.last_adc_clk_corrections) {
            clk_printf("%-12s adc_clock_system() base=%.6lf man_adj=%d clk=%.6lf(%d)\n", "CLK",
                       clk.adc_clock_base / 1e6, clk.manual_adj, new_clk / 1e6, clk.adc_clk_corrections);
            clk.last_adc_clk_corrections = clk.adc_clk_corrections;
        }
    }

    return new_clk;
}

void clock_manual_adj(int manual_adj) {
    clk.manual_adj = manual_adj;
    adc_clock_system();
    clk.adc_clk_corrections++;
}

// Compute corrected ADC clock based on GPS time.
// Called on each GPS solution.
void clock_correction(double t_rx, u64_t ticks) {
    // record stats
    clk.gps_secs = t_rx;
    clk.ticks = ticks;

    bool initial_temp_correction = (clk.adc_clk_corrections <= 5);

    if (clk.do_corrections == ADC_CLK_CORR_DISABLED) {
        clk.is_corr = false;
        clk_printf("CLK CORR DISABLED\n");
        return;
    }

    u64_t diff_ticks = ticks;
    double gps_secs = 1.0;
    double new_adc_clock = diff_ticks / gps_secs;

// First correction allows wider window to capture temperature error.
// Subsequent corrections use a much tighter window to remove bad GPS solution outliers.
// Also use wider window if too many sequential solutions outside window.
#define MAX_OUTSIDE 8
    bool first_time_temp_correction = (clk.adc_clk_corrections == 0);

    double offset_window =
        (first_time_temp_correction || outside_window > MAX_OUTSIDE) ? PPM_TO_HZ(ADC_CLOCK_TYP, ADC_CLOCK_PPM_TYP) : PPM_TO_HZ(ADC_CLOCK_TYP, 1);
    double offset = new_adc_clock - clk.adc_clock_base; // offset from previous clock value

    // limit offset to a window to help remove outliers
    if (offset < -offset_window || offset > offset_window) {
        outside_window++;
        clk_printf("CLK BAD %4d offHz %2.0f winHz %2.0f SYS %.6f NEW %.6f GT %6.3f ticks %08x|%08x\n",
                   outside_window, offset, offset_window,
                   clk.adc_clock_base / 1e6, new_adc_clock / 1e6, gps_secs, PRINTF_U64_ARG(ticks));
        return;
    }
    outside_window = 0;

    // first correction handles XO temperature offset
    if (first_time_temp_correction) {
        clk.temp_correct_offset = offset;
    }

    // Perform modified moving average which seems to keep up well during
    // diurnal temperature drifting while dampening-out any transients.
    // Keeps up better than a simple cumulative moving average.

    static double adc_clock_mma;
#define MMA_PERIODS 32
    if (adc_clock_mma == 0) adc_clock_mma = new_adc_clock;
    adc_clock_mma = ((adc_clock_mma * (MMA_PERIODS - 1)) + new_adc_clock) / MMA_PERIODS;
    clk.adc_clk_corrections++;
    clk.adc_gps_clk_corrections++;

#ifdef CLK_PRINTF
    double diff_mma = adc_clock_mma - clk.adc_clock_base;
#endif
    clk_printf("CLK CORR %3d offHz %2.0f winHz %4.0lf MMA %.3lf(%6.3f) %4.1f NEW %.3lf\n",
               clk.adc_clk_corrections, offset, offset_window,
               adc_clock_mma, diff_mma, offset, new_adc_clock);

    clk.manual_adj = 0; // remove any manual adjustment now that we're automatically correcting
    clk.adc_clock_base = adc_clock_mma;

    // update the system with the reflction of si5351
    // printf("CLOCK Drift = %f\n", (adc_clock_mma - ADC_CLOCK_TYP) * 1.0f / (ADC_CLOCK_TYP/MHz)*1000.0f);
    clock_correction((adc_clock_mma - ADC_CLOCK_TYP) * 1.0f / (ADC_CLOCK_TYP / MHz) * 1000.0f);

#if 0
        if (!ns_nom) ns_nom = clk.adc_clock_base;
        int bin = ns_nom - clk.adc_clock_base;
        ns_bin[bin+512]++;
#endif

    // If in one of the clock correction-limiting modes we still maintain the corrected clock values
    // for the benefit of GPS timestamping (i.e. clk.* values updated).
    // But don't update the conn->adc_clock_corrected values which keeps the audio and waterfall
    // NCOs from changing during the blocked intervals.
    if (clk.do_corrections > ADC_CLK_CORR_CONTINUOUS) {
        bool ok = false;
        const char* s;
        int min, sec;
        utc_hour_min_sec(NULL, &min, &sec);

        switch (clk.do_corrections) {

        case ADC_CLK_CORR_EVEN_2_MIN:
            s = "even 2 min";
            ok = ((min & 1) && sec > 51);
            break;

        case ADC_CLK_CORR_5_MIN:
            s = "5 min";
            ok = ((min % 5) == 4 && sec > 50);
            break;

        case ADC_CLK_CORR_15_MIN:
            s = "15 min";
            ok = ((min % 15) == 14 && sec > 50);
            break;

        case ADC_CLK_CORR_30_MIN:
            s = "30 min";
            ok = ((min % 30) == 29 && sec > 52);
            break;

        default:
            s = "???";
            break;
        }

        if (!initial_temp_correction) {
            clk_printf("CLK %02d:%02d every %s, %s\n", min, sec, s, ok ? "OK" : "skip");
            clk.is_corr = ok;
            if (!ok) return;
        }
        else {
            clk_printf("CLK %02d:%02d every %s, but initial temp correction\n", min, sec, s);
        }
    }

    clk.is_corr = true;

    // Even if !adjust_clock mode is set adjust for first_time_temp_correction.
    // If any of the correction-limiting modes are in effect, apply during the entire window
    // to make sure correction is propagated.
    u4_t now = timer_sec();
    static u4_t last;
    if (now > (last + 10) || clk.do_corrections > ADC_CLK_CORR_CONTINUOUS) {
        clk_printf("%-12s CONN ", "CLK");
        for (conn_t* c = conns; c < &conns[N_CONNS]; c++) {
            if (!c->valid || (!c->adjust_clock && !first_time_temp_correction)) continue;

            // adc_clock_corrected is what the sound and waterfall channels use for their NCOs
            c->adc_clock_corrected = clk.adc_clock_base;
            clk_printf("%d ", c->self_idx);
        }
        clk_printf("\n");
        clk_printf("%-12s APPLY clk=%.3lf(%d)\n", "CLK", clk.adc_clock_base, clk.adc_clk_corrections);
        last = now;
    }
}
