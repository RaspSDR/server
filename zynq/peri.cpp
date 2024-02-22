#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "misc.h"
#include "peri.h"
#include "spi.h"
#include "coroutines.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static bool init;

void peri_init()
{
    int fd;

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