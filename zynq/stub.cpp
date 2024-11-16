#include "kiwi.h"
#include "eeprom.h"

bool background_mode = false;

int eeprom_check() {
    int status;
    int serialno = -1;

    // read from enviroment block
    kstr_t* reply = non_blocking_cmd("fw_printenv serial --noheader", &status);
    if (status == 0) {
        serialno = atoi(kstr_sp(reply));
    }

    kstr_free(reply);

    return serialno;
}

uint32_t eeprom_refclock() {
    int status;
    uint32_t refclock = 24576000; // default to 27M

    // read from enviroment block
    kstr_t* reply = non_blocking_cmd("fw_printenv refclock --noheader", &status);
    if (status == 0) {
        refclock = atoi(kstr_sp(reply));
    }

    kstr_free(reply);

    return refclock;
}