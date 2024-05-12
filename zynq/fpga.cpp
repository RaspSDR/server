#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "clk.h"
#include "mem.h"
#include "misc.h"
#include "web.h"
#include "peri.h"
#include "coroutines.h"
#include "debug.h"
#include "fpga.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <semaphore.h>

volatile FPGA_Config *fpga_config;
const volatile FPGA_Status *fpga_status;
const volatile uint32_t *fpga_pps_data;
const volatile int32_t *fpga_rx_data;
const volatile uint32_t *fpga_wf_data[4];

static int wf_channels;
static sem_t wf_sem;
static std::atomic<int> wf_using[4];

////////////////////////////////
// FPGA DNA
////////////////////////////////

u64_t fpga_dna()
{
    return fpga_status->fpga_dna;
}

//------------------//
void fpga_init()
{
    int fd;
    if ((fd = open("/dev/mem", O_RDWR)) < 0)
    {
        panic("Cannot open /dev/mem.");
    }

    fpga_config = (FPGA_Config *)mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x40000000);
    fpga_status = (FPGA_Status *)mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, fd, 0x41000000);
    fpga_rx_data = (const int32_t *)mmap(NULL, 2 * sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, fd, 0x42000000);

    for (int i = 0; i < 4; i++)
    {
        fpga_wf_data[i] = (const uint32_t *)mmap(NULL, 1 * sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, fd, 0x43000000 + 0x01000000 * i);
    }

    fpga_pps_data =  (const uint32_t *)mmap(NULL, 1 * sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, fd, 0x47000000);

    close(fd);

#if 0
  fpga_config->reset = 0x1f;

  for (int i = 0; i < 8; ++i)
  {
    fpga_config->rx_freq[i] = (uint32_t)floor(7e6 / 125.0e6 * (1 << 30) + 0.5);
  }

  for (int i = 0; i < 4; i++)
  {
    fpga_config->wf_config[i].wf_freq = (uint32_t)floor(7e6 / 125.0e6 * (1 << 30) + 0.5);;
    fpga_config->wf_config[i].wf_decim = 2048;    //1 << (i * 2 + 5);
    printf("CHN[%d] Decimate set to %d\n", i, 2048);    //1 << (i * 2 + 5));
  }

  printf("RX  \tWF0\tWF1\tWF2\tWF3\tPPS\tRX_I\tRX_Q\t\tWF0\tWF1\tWF2\tWF3\n");
  for (int i = 0; i < 1500; i++)
  {
    int data0,data00, data1, data2, data3, data4;
    printf("%d\t%d\t%d\t%d\t%d\t%d\t", fpga_status->rx_fifo, fpga_status->wf_fifo[0], fpga_status->wf_fifo[1], fpga_status->wf_fifo[2], fpga_status->wf_fifo[3], fpga_status->pps_fifo);
    data0 = *fpga_rx_data;
    data00 = *fpga_rx_data;
    data1 = *fpga_wf_data[0];
    data2 = *fpga_wf_data[1];
    data3 = *fpga_wf_data[2];
    data4 = *fpga_wf_data[3];

    printf("%08x %08x | %08x %08x %08x %08x |\n", data0, data00, data1, data2, data3, data4);
    //usleep(100);
  }
#endif

    fpga_config->reset = RESET_RX;

    wf_channels = (fpga_status->signature >> 8) & 0x0f;

    sem_init(&wf_sem, 0, wf_channels);
}

int fpga_get_wf(int rx_chan, int decimate, uint32_t freq)
{
  sem_wait(&wf_sem);

  for (int i = 0; i < wf_channels; i++)
  {
    int empty = 0;
    bool exchanged = wf_using[i].compare_exchange_strong(empty, rx_chan+10);

    if (exchanged)
    {
      // allocate wf channel i for rx_channel
      fpga_config->wf_config[i].wf_decim = decimate;
      fpga_config->wf_config[i].wf_freq = (-freq) >> 2;

      TaskSleepUsec(5);
      fpga_config->reset |= RESET_WF0 << i;

      return i;
    }
  }

  panic("WF is runout");

  return -1;
}

void fpga_free_wf(int wf_chan, int rx_chan)
{
    fpga_config->reset &= ~(RESET_WF0 << wf_chan);
    rx_chan += 10;

    bool exchanged = wf_using[wf_chan].compare_exchange_strong(rx_chan, 0);
    assert(exchanged);

    sem_post(&wf_sem);
}
