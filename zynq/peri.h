#pragma once

#include <sys/wait.h>

#define GPIO_ANTENNA      (1 << 0)
#define GPIO_ANTENNA0     (1 << 0)
#define GPIO_ANTENNA1     (1 << 1)
#define GPIO_ANTENNA2     (1 << 2)
#define GPIO_ANTENNA3     (1 << 3)
#define GPIO_ANTENNA4     (1 << 4)
#define GPIO_ANTENNA5     (1 << 5)
#define GPIO_ANNENNA_MASK (0x7f)

#define GPIO_DITHER (1 << 6)
#define GPIO_PGA    (1 << 7)
#define GPIO_LED    (1 << 8)

void peri_init();
void peri_free();

void rf_enable_airband(bool enabled);
void rf_attn_set(float attn_dB);

void sd_enable(bool write);
void clock_correction(float freq_error);

extern u64_t fpga_dna();

extern void fpga_start_rx();
extern void fpga_read_rx(void* buf, uint32_t size);
extern void fpga_rxfreq(int rx_chan, uint64_t freq);

extern void fpga_start_pps();
extern uint64_t fpga_read_pps();

extern int fpga_set_antenna(int mask);
extern int fpga_set_pga(bool enabled);
extern int fpga_set_dither(bool enabled);
extern int fpga_set_led(bool enabled);

extern uint32_t fpga_signature();

extern void fpga_setovmask(uint32_t mask);
extern void fpga_setadclvl(uint32_t val);

extern int fpga_reset_wf(int wf_chan, bool cont);
extern int fpga_wf_param(int wf_chan, int decimate, uint64_t freq);
extern int fpga_get_wf(int rx_chan);
extern void fpga_free_wf(int wf_chan, int rx_chan);
extern void fpga_read_wf(int wf_chan, void* buf, uint32_t size);
