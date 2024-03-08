#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <sys/mman.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define RESET_RX (1 << 0)
#define RESET_WF0 (1 << 1)
#define RESET_WF1 (1 << 2)
#define RESET_WF2 (1 << 3)
#define RESET_WF3 (1 << 4)

typedef struct
{
  uint16_t reset;
  uint16_t siggen;
  uint32_t rx_freq[8];
  struct
  {
    uint32_t wf_freq;
    uint32_t wf_decim;
  } wf_config[4];
  uint32_t siggen_freq;
}__attribute__((packed)) FPGA_Config;

typedef struct
{
  uint32_t rx_fifo;
  uint32_t wf_fifo[4];
  uint32_t pps_fifo;
  uint64_t fpga_dna;
}__attribute__((packed)) FPGA_Status;

typedef struct
{
  uint32_t rx_data;
  uint32_t wf_data[4];
}__attribute__((packed)) FPGA_Data;

volatile FPGA_Config *fpga_config;
volatile const FPGA_Status *fpga_status;
volatile FPGA_Data *fpga_data;
const volatile uint32_t *fpga_pps_data;

void print_current_time_with_ms(void)
{
  long ms;  // Milliseconds
  time_t s; // Seconds
  struct timespec spec;

  clock_gettime(CLOCK_REALTIME, &spec);

  s = spec.tv_sec;
  ms = round(spec.tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds
  if (ms > 999)
  {
    s++;
    ms = 0;
  }

  printf("Current time: %" PRIdMAX ".%03ld seconds since the Epoch\n",
         (intmax_t)s, ms);
}

#define ADC_FREQ (125.0)
#define MAKE_FREQ(freq, corr) ((uint32_t)floor((1.0 + 1.0e-6 * corr) * freq / ADC_FREQ * (1 << 30) + 0.5))

int main(int argc, char *argv[])
{
  int fd, offset, length, i, j;
  time_t t;
  struct tm *gmt;
  uint64_t *buffer;
  char date[12];
  char name[64];
  double dialfreq;
  double corr = 0.0;
  double freq[8] = {1000.0, 20000.0, 300000.0, 400000.0, 500000.0, 600000.0, 700000.0, 800000.0};
  int chan[8] = {1, 1, 1, 1, 1, 1, 1, 1};
  uint8_t value = 0;

  if ((fd = open("/dev/mem", O_RDWR)) < 0)
  {
    fprintf(stderr, "Cannot open /dev/mem.\n");
    return EXIT_FAILURE;
  }

  fpga_config = mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x40000000);
  fpga_status = mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, fd, 0x41000000);
  fpga_data = mmap(NULL, 32 * sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, fd, 0x42000000);


    fpga_pps_data =  (const uint32_t *)mmap(NULL, 1 * sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, fd, 0x47000000);


  fpga_config->reset = 0;
  fpga_config->reset = 0xffff;

  for (i = 0; i < 8; ++i)
  {
    fpga_config->rx_freq[i] = MAKE_FREQ(freq[i], corr);
  }

  for (int i = 0; i < 4; i++)
  {
    fpga_config->wf_config[i].wf_freq = MAKE_FREQ(freq[i], corr);
    fpga_config->wf_config[i].wf_decim = 1 << (i + 3);
  }

  printf("DNA: %llx\n", fpga_status->fpga_dna);
  printf("Count RX Addr&: %x\n", &fpga_status->rx_fifo);
  printf("Count WF0 Addr&: %x\n", &fpga_status->wf_fifo[0]);
  printf("Count WF1 Addr&: %x\n", &fpga_status->wf_fifo[1]);
  printf("Count WF2 Addr&: %x\n", &fpga_status->wf_fifo[2]);
  printf("Count WF3 Addr&: %x\n", &fpga_status->wf_fifo[3]);

  printf("RX\tWF0\tWF1\tWF2\tWF3\tPPS\n");
  for (int i = 0; i < 1000; i++)
  {
    printf("%d\t%d\t%d\t%d\t%d\t%d\n", fpga_status->rx_fifo, fpga_status->wf_fifo[0], fpga_status->wf_fifo[1], fpga_status->wf_fifo[2], fpga_status->wf_fifo[3], fpga_status->pps_fifo);
    if (fpga_status->pps_fifo > 0)
    {
      printf("PPS=%lu\n", *fpga_pps_data);
    }
    sleep(1);
  }

  fpga_config->reset &= ~RESET_RX;
  fpga_config->reset |= RESET_RX;

  offset = 0;

  struct timespec start_spec, stop_spec;

  clock_gettime(CLOCK_REALTIME, &start_spec);


  printf("DNA: %llx\n", fpga_status->fpga_dna);
  printf("Count Addr&: %x\n", &fpga_status->rx_fifo);

  while (offset < 24000)
  {

    while (fpga_status->rx_fifo < 500) {
      //printf("Count: %d\n", fpga_status->rx_fifo);
      usleep(1000);
    }
      

    for (i = 0; i < 250; ++i)
    {
      for (j = 0; j < 8; ++j)
      {
        int data0 = fpga_data->rx_data;
        int data1 = fpga_data->rx_data;        
      }
    }

    offset += 250;
    // printf("%d\n", offset);
  }

  clock_gettime(CLOCK_REALTIME, &stop_spec);

  printf("RX: sample rate= %f (should close to 12,000, otherwise there is bug)\n", 24000.0 / (stop_spec.tv_sec - start_spec.tv_sec + (stop_spec.tv_nsec - start_spec.tv_nsec) * 1e-9));
  printf("Count Addr&: %x\n", &fpga_status->wf_fifo[0]);

  for (int i = 0; i < 1; i++)
  {
       // test each WF channels
    fpga_config->reset &= ~(RESET_WF0 << i);
    fpga_config->reset |= RESET_WF0 << i;

    clock_gettime(CLOCK_REALTIME, &start_spec);
    offset = 0;

    while (offset < 24000)
    {
      while (fpga_status->wf_fifo[i] < 250) {
        printf("Count: %d\n", fpga_status->wf_fifo[i]);
        usleep(1000);
      }
 
      for (i = 0; i < 250; ++i)
      {
        int data = fpga_data->wf_data[i];
      }

      offset += 250;
      printf("%d\n", offset);
    }

    clock_gettime(CLOCK_REALTIME, &stop_spec);
    printf("RX: sample rate= %f (should close to 12000, otherwise there is bug)\n", 24000.0 / (stop_spec.tv_sec - start_spec.tv_sec + (stop_spec.tv_nsec - start_spec.tv_nsec) * 1e-9));
  }

  return EXIT_SUCCESS;
}
