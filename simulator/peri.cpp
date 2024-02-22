#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "misc.h"
#include "peri.h"
#include "spi.h"
#include "spi_dev.h"
#include "coroutines.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

void gpio_test(int gpio_test)
{}

static struct timespec start;

void peri_init()
{
    clock_gettime(CLOCK_MONOTONIC, &start);
}

#define SIGNAL_FREQUENCY 12000 // 12kHz
#define SIGNAL_INTERVAL (1000000000 / SIGNAL_FREQUENCY)
bool sample_12k()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    bool ret = ((now.tv_sec - start.tv_sec) * 1000000000 + now.tv_nsec - start.tv_nsec >= SIGNAL_INTERVAL * nrx_samps);
    start = now;

    return ret;
}
