#include "system.h"

#include <stdio.h>
#include <stdlib.h>

void system_reboot()
{
    const char* events = getenv("WEBSDR_REBOOT_EVENTS");
    if (events != NULL) {
        if (events[0] != '/') {
            fprintf(stderr, "native harness: WEBSDR_REBOOT_EVENTS must be absolute\n");
            exit(EXIT_FAILURE);
        }
        FILE* fp = fopen(events, "a");
        if (fp == NULL) {
            perror("native harness: opening reboot event file");
            exit(EXIT_FAILURE);
        }
        bool written = fputs("reboot\n", fp) >= 0;
        if (fclose(fp) != 0 || !written) {
            perror("native harness: writing reboot event");
            exit(EXIT_FAILURE);
        }
    }
    printf("native harness: reboot ignored\n");
    fflush(stdout);
}

void system_halt()
{
    printf("native harness: halt ignored\n");
}

void system_poweroff()
{
    printf("native harness: poweroff ignored\n");
}
