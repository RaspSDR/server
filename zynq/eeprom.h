#pragma once

typedef enum { SERNO_READ, SERNO_WRITE, SERNO_ALLOC } next_serno_e;

int eeprom_next_serno(next_serno_e type, int set_serno);
int eeprom_check(model_e *model = NULL);
void eeprom_write(next_serno_e type, int serno = 0, int model = 0);
void eeprom_update();
void eeprom_test();