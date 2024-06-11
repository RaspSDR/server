#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "misc.h"
#include "peri.h"
#include "coroutines.h"
#include "clk.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <atomic>

#include "../si5351/LinuxInterface.h"
#include "../si5351/si5351.h"

#include "arm_math.h"

static bool init;

static const uint8_t buss_id = 0;
static const uint8_t chip_addr = 0x60;

static I2CInterface *i2c;
static Si5351 *si5351;

#define AD8370_SET _IOW('Z', 0, uint32_t)
#define MODE_SET _IOW('Z', 1, uint32_t)
#define CLK_SET _IOW('Z', 2, uint32_t)

int ad8370_fd;

void peri_init()
{
    if (init)
        return;

    scall("/dev/ad8370", ad8370_fd = open("/dev/ad8370", O_RDWR | O_SYNC));
    if (ad8370_fd <= 0)
    {
        panic("Failed to open kernel driver");
    }

    i2c = new LinuxInterface(buss_id, chip_addr);
    si5351 = new Si5351(chip_addr, i2c);

    int int_clk;
    uint32_t ref_clk;
    if (clk.ext_ADC_clk)
    {
        int_clk = 0;
    }
    else
    {
        int_clk = 1;
    }

    ioctl(ad8370_fd, CLK_SET, &int_clk);

    bool i2c_found = si5351->init(SI5351_CRYSTAL_LOAD_0PF, clk.clock_ref, 0);
    if (!i2c_found)
    {
        panic("i2c si5351 is not found\n\n");
        return;
    }
    else
    {
        int ret = si5351->set_freq((uint64_t)(ADC_CLOCK_NOM * 100), SI5351_CLK0);
        printf("i2c si5351 initialized, error=%d\n", ret);
    }

    if (clk.gpsdo_ext_clk > 0)
    {
        si5351->set_freq((uint64_t)(clk.gpsdo_ext_clk * 100), SI5351_CLK2);
    }

    // set airband mode
    rf_enable_airband(kiwi.airband);

    // set default attn to 0
    rf_attn_set(0);

    init = TRUE;
}

void rf_attn_set(float f)
{
    if (f > 0)
        return;

    int gain = (int)(-f * 2);

    printf("Set PE4312 with %d/0x%x\n", gain, gain);
    if (ioctl(ad8370_fd, AD8370_SET, &gain) < 0)
    {
        printf("AD8370 set RF failed: %s\n", strerror(errno));
    }

    return;
}

void rf_enable_airband(bool enabled)
{
    int data = (int)enabled;
    if (ioctl(ad8370_fd, MODE_SET, &data) < 0)
    {
        printf("AD8370 set mode failed\n");
    }

    return;
}

void peri_free()
{
    assert(init);
    close(ad8370_fd);
    si5351->set_freq((uint64_t)(0 * 100), SI5351_CLK0);
}

static std::atomic<int> write_enabled(0);
void sd_enable(bool write)
{
    if (write)
    {
        int v = std::atomic_fetch_add(&write_enabled, 1);

        if (v == 0)
        {
            system("mount -o rw,remount /media/mmcblk0p1");
        }
    }
    else
    {
        int v = std::atomic_fetch_add(&write_enabled, -1);
        if (v == 1)
        {
            system("mount -o ro,remount /media/mmcblk0p1");
        }
    }
}

static arm_pid_instance_f32 PID;
static float32_t Kp = 1.4f;  // Proportional gain
static float32_t Ki = 0.15f;  // Integral gain
static float32_t Kd = 0.01f; // Derivative gain
static float32_t setpoint = 1.0f;  // Desired setpoint
static float32_t measured_value = 0.0f; // Current measured value
static float32_t control_output = 0.0f; // Control output

static int last = 100;

void clock_correction(float error)
{
    if (last < 3)
    {
        last++;
        return;
    }
    last = 0;

    if (PID.Kp == 0.0f)
    {
        // initialize
        PID.Kp = Kp;
        PID.Ki = Ki;
        PID.Kd = Kd;
        arm_pid_init_f32(&PID, 1); // Initialize the PID instance with reset state
    }

    control_output = arm_pid_f32(&PID, error);

    si5351->set_correction((int)control_output, SI5351_PLL_INPUT_XO);

    //printf("Set correction to %d\n", (int)control_output);
}