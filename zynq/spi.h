#pragma once
#include "kiwi.h"

#include <inttypes.h>
#include <sys/wait.h>

typedef enum { // Embedded CPU commands, order must match 'Commands:' table in .asm code

    // general
    CmdPing = 0,
    CmdSetMem,
    CmdPing2,
    CmdGetCPUCtr,
    CmdCtrlClrSet,
    CmdCtrlPulse,
    CmdCtrlGet,
	CmdGetMem,
	CmdGetStatus,
	CmdFlush,
    CmdTestRead,
    CmdUploadStackCheck,
    CmdGetSPRP,

	// SDR
    CmdSetRXFreq,
    CmdSetRXNsamps,
    CmdSetGenFreq,
    CmdSetGenAttn,
    CmdGetRX,
    CmdClrRXOvfl,
    CmdSetWFFreq,
	CmdSetWFDecim,
    CmdWFReset,
    CmdGetWFSamples,
    CmdGetWFContSamps,
	CmdSetOVMask,
	CmdGetADCCtr,
	CmdSetADCLvl,

	// GPS
#ifdef USE_GPS
    CmdSample,
    CmdSetChans,
    CmdSetMask,
    CmdSetRateCG,
    CmdSetRateLO,
    CmdSetGainCG,
    CmdSetGainLO,
    CmdSetSat,
    CmdSetE1Bcode,
    CmdSetPolarity,
    CmdPause,
    CmdGetGPSSamples,
    CmdGetChan,
    CmdGetClocks,
    CmdGetGlitches,
    CmdIQLogReset,
    CmdIQLogGet,
#endif

    CmdCheckLast,
    
    // pseudo for debugging
    CmdPumpFlush,

#ifndef USE_GPS
    CmdSample = 0,
    CmdSetChans = 0,
    CmdSetMask = 0,
    CmdSetRateCG = 0,
    CmdSetRateLO = 0,
    CmdSetGainCG = 0,
    CmdSetGainLO = 0,
    CmdSetSat = 0,
    CmdSetE1Bcode = 0,
    CmdSetPolarity = 0,
    CmdPause = 0,
    CmdGetGPSSamples = 0,
    CmdGetChan = 0,
    CmdGetClocks = 0,
    CmdGetGlitches = 0,
    CmdIQLogReset = 0,
    CmdIQLogGet = 0,
#endif

} SPI_CMD;

void spi_init();

void spi_set3(SPI_CMD cmd, uint16_t wparam, uint32_t lparam, uint16_t w2param);
void spi_set(SPI_CMD cmd, uint16_t wparam=0, uint32_t lparam=0);