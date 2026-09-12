#include "system.h"

#include <stdio.h>

void system_reboot()
{
    printf("native harness: reboot ignored\n");
}

void system_halt()
{
    printf("native harness: halt ignored\n");
}

void system_poweroff()
{
    printf("native harness: poweroff ignored\n");
}
