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

#include "../si5351/LinuxInterface.h"
#include "../si5351/si5351.h"

static bool init;

static const uint8_t buss_id = 0;
static const uint8_t chip_addr = 0x60;

static I2CInterface *i2c;
static Si5351 *si5351;

#define AD8370_SET _IOW('Z', 0, uint32_t)
#define MODE_SET _IOW('Z', 1, uint32_t)
int ad8370_fd;

void peri_init()
{
    if (init)
        return;

    i2c = new LinuxInterface(buss_id, chip_addr);
    si5351 = new Si5351(chip_addr, i2c);
  
    bool i2c_found = si5351->init(SI5351_CRYSTAL_LOAD_0PF, 27000000, 0);
    if(!i2c_found)
    {
        printf("i2c si5351 is not found\n\n");
    }
    else
    {
        int ret = si5351->set_freq((uint64_t)(ADC_CLOCK_NOM * 100), SI5351_CLK0);
        printf("i2c si5351 initialized, error=%d\n", ret);
    }

    scall("/dev/ad8370", ad8370_fd = open("/dev/ad8370", O_RDWR|O_SYNC));
    if (ad8370_fd > 0)
    {
        int data = 0;

        // set airband mode
        rf_enable_airband(kiwi.airband);

        // set default attn to 0
        rf_attn_set(0);
    }

    init = TRUE;
}

void rf_attn_set(float f) {

    int gain = (int)(f * 2);

    if (ad8370_fd > 0)
    {
        printf("Set PE4312 with %d/0x%x\n", gain, gain);
        if(ioctl(ad8370_fd, AD8370_SET, &gain) < 0)
        {
            printf("AD8370 set RF failed: %s\n", strerror(errno));
        }
    }

    return;
}

void rf_enable_airband(bool enabled)
{
    int data = (int)enabled;
    if (ad8370_fd > 0)
    {
        if(ioctl(ad8370_fd, MODE_SET, &data) < 0)
        {
            printf("AD8370 set mode failed\n");
        }
    }

    return;
}

void peri_free()
{
    assert(init);
    close(ad8370_fd);
    si5351->set_freq((uint64_t)(0 * 100), SI5351_CLK0);
}