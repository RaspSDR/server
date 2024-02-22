#pragma once

#include <sys/wait.h>
#include "fpga.h"

void peri_init();
void gpio_test(int gpio_test);
void peri_free();

#define FPGA_ID_RX14_WF4 4
