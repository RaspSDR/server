#include "spi.h"
#include "fpga.h"


void spi_init(){}
void spi_stats(){}

void spi_set3(SPI_CMD cmd, uint16_t wparam, uint32_t lparam, uint16_t w2param)
{
    switch (cmd)
    {
    case CmdSetRXFreq:
        //printf("CmdSetRXFreq[%d]=%d + %d\n", wparam, lparam, w2param);
        fpga_config->rx_freq[wparam] = lparam >> 2;
        break;
    
    case CmdSetWFFreq:
        //printf("CmdSetWFFreq[%d]=%d + %d\n", wparam, -lparam, w2param);
        fpga_config->wf_config[wparam].wf_freq = (-lparam) >> 2;
        break;

    case CmdSetWFDecim:
        printf("CmdSetWFDecim[%d]=%d\n", wparam, lparam);
        fpga_config->reset &= ~(RESET_WF0 << wparam);
        fpga_config->wf_config[wparam].wf_decim = lparam;
        break;

    case CmdSetGenFreq:
        //printf("CmdSetGenFreq %d=%d\n", wparam, lparam);
        // fpga_config->gen_freq = wparam >> 2;
        break;

    case CmdSetGenAttn:
        //printf("CmdSetGenAttn %d=%d\n", wparam, lparam);
        break;

    case CmdWFReset:
        fpga_config->reset &= ~(RESET_WF0 << wparam);
        fpga_config->reset |= RESET_WF0 << wparam;
        break;

    case CmdSetRXNsamps:
        printf("CmdSetRXNsamps %d\n", wparam);
        if (wparam == 0) {
            // reset RX
            fpga_config->reset &= ~RESET_RX;
        } else {
            fpga_config->reset |= RESET_RX;
        }
        break;

    case CmdSetOVMask:
        printf("CmdSetOVMask %d, %d\n", wparam, lparam);
        break;

    default:
        printf("Cannot handle spi_set3 spi command %d: %d, %d\n", cmd, wparam, lparam);
    }
}

void spi_set(SPI_CMD cmd, uint16_t wparam, uint32_t lparam)
{
    spi_set3(cmd, wparam, lparam, 0);
}