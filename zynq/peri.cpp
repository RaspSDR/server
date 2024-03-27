#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "misc.h"
#include "peri.h"
#include "coroutines.h"
#include "clk.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "../si5351/LinuxInterface.h"
#include "../si5351/si5351.h"

static bool init;

static const uint8_t buss_id = 0;
static const uint8_t chip_addr = 0x60;

static I2CInterface *i2c;
static Si5351 *si5351;
void peri_init()
{
    i2c = new LinuxInterface(buss_id, chip_addr);
    si5351 = new Si5351(chip_addr, i2c);
  
    bool i2c_found = si5351->init(SI5351_CRYSTAL_LOAD_0PF, 25000000, 0);
    if(!i2c_found)
    {
        printf("i2c si5351 is not found\n");
    }
    else
    {
        si5351->set_freq(ADC_CLOCK_NOM, SI5351_CLK0);
        printf("i2c si5351 initialized\n");
    }

    if (init)
        return;

    init = TRUE;
}

void gpio_test(int gpio_test_pin) {
}

void peri_free()
{
    assert(init);
}