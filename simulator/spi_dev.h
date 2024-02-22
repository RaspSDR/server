#pragma once

#include "spi.h"

typedef struct {
    SPI_MISO dpump_miso;
    SPI_MISO wf_miso[MAX_RX_CHANS];
    SPI_MISO misc_miso[2];
    SPI_MISO spi_junk_miso, pingx_miso;
    SPI_MOSI misc_mosi;
}spi_shmem_t;

#define SPI_SHMEM   spi_shmem_p
extern spi_shmem_t  *spi_shmem_p;

// values compatible with spi_pio:spi_speed
#define	SPI_48M			0
#define	SPI_24M			1
#define	SPI_12M			2
#define	SPI_6M			3
#define	SPI_3M			4
#define	SPI_1_5M		5

