#include "kiwi.h"
#include "eeprom.h"

bool background_mode = false;

bool backup_in_progress = false;
bool sd_copy_in_progress = false;

void rf_attn_set(float f) {}

int eeprom_next_serno(next_serno_e type, int set_serno) {return 0x1007;}
int eeprom_check(model_e *model) {return -1;}
void eeprom_write(next_serno_e type, int serno, int model){}
void eeprom_update() {}
void eeprom_test() {}