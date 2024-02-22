#include "kiwi.h"
#include "eeprom.h"

bool background_mode = false;

void gps_fe_init() {}

void rf_attn_set(float f) {}

int eeprom_next_serno(next_serno_e type, int set_serno) {return 0x1007;}
int eeprom_check(model_e *model) {return -1;}
void eeprom_write(next_serno_e type, int serno, int model){}
void eeprom_update() {}
void eeprom_test() {}

void system_reboot()
{
    printf("reboot\n");
    exit(0);
}

void system_halt()
{
    printf("halt\n");
    exit(0);
}

void system_poweroff()
{
    printf("poweroff\n");
    exit(0);
}

void ctrl_clr_set(u2_t clr, u2_t set){}
void ctrl_positive_pulse(u2_t bits){}
void ctrl_set_ser_dev(u2_t ser_dev){}
void ctrl_clr_ser_dev(){}

void fpga_init() {}
u64_t fpga_dna() {return 0x1234567;}

#include "spi.h"
#include "spi_dev.h"
#include "fpga.h"

spi_t spi;
u4_t spi_retry;

static spi_shmem_t spi_shmem;
spi_shmem_t  *spi_shmem_p = &spi_shmem;


void spi_init(){}
void spi_stats(){}

void spi_set3(SPI_CMD cmd, uint16_t wparam, uint32_t lparam, uint16_t w2param)
{
    switch (cmd)
    {
    case CmdSetRXFreq:
        printf("CmdSetRXFreq[%d]=%d\n", wparam, lparam);
        break;
    
    case CmdSetWFFreq:
        printf("CmdSetWFFreq[%d]=%d\n", wparam, lparam);
        break;

    case CmdSetGenFreq:
        printf("CmdSetGenFreq %d=%d\n", wparam, lparam);
        break;

    case CmdSetGenAttn:
        printf("CmdSetGenAttn %d=%d\n", wparam, lparam);
        break;

    case CmdWFReset:
        //printf("CmdWFReset\n");
        break;

    case CmdSetRXNsamps:
        printf("CmdSetRXNsamps %d\n", wparam);
        break;

    case CmdSetOVMask:
        printf("CmdSetOVMask %d, %d\n", wparam, lparam);
        break;

    default:
        printf("Cannot handle spi command %d: %d, %d\n", cmd, wparam, lparam);
    }
}

void spi_set(SPI_CMD cmd, uint16_t wparam, uint32_t lparam)
{
    spi_set3(cmd, wparam, lparam, 0);
}

void spi_get_noduplex(SPI_CMD cmd, SPI_MISO *rx, int bytes, uint16_t wparam, uint32_t lparam)
{
    spi_get3_noduplex(cmd, rx, wparam, lparam, 0);
}

void spi_get3_noduplex(SPI_CMD cmd, SPI_MISO *rx, int bytes, uint16_t wparam, uint16_t w2param, uint16_t w3param)
{
    switch(cmd) {
        case CmdGetWFSamples:
        case CmdGetWFContSamps:
        case CmdGetSPRP:
        case CmdGetADCCtr:
        break;

        case CmdGetRX:
        // printf("CmdGetRX : size=%d, %d %d %d", bytes, wparam, w2param, w3param);
        break;
    }
}

void led_task(void *param)
{}