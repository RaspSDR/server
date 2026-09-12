#include <cstdint>

#include "eeprom.h"

bool background_mode = true;

int eeprom_check()
{
    return 888;
}

uint32_t eeprom_refclock()
{
    return 24576000;
}
