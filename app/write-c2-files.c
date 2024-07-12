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

#define RESET_RX    (1 << 0)
#define RESET_WF(n) (1 << (n + 1))

#define RESET_LEDON (1 << 7)
#define RESET_PPS   (1 << 8)

#define GPIO_ANTENNA(n) (1 << (n + 0))

#define GPIO_DITHER (1 << 6)
#define GPIO_PGA    (1 << 7)

typedef struct {
    uint32_t reset;
    uint32_t rx_freq[16];
    struct {
        uint32_t wf_freq;
        uint32_t wf_decim;
    } wf_config[4];
    uint8_t gpios;
} __attribute__((packed)) FPGA_Config;

typedef struct {
    uint32_t signature;
    uint32_t rx_fifo;
    uint32_t wf_fifo[4];
    uint32_t pps_fifo;
    uint64_t fpga_dna;
    uint64_t tsc;
} __attribute__((packed)) FPGA_Status;

const volatile int32_t* fpga_rx_data;
const volatile uint32_t* fpga_wf_data[4];
const volatile uint32_t* fpga_pps_data;
volatile FPGA_Config* fpga_config;
const volatile FPGA_Status* fpga_status;

void print_current_time_with_ms(void) {
    long ms;  // Milliseconds
    time_t s; // Seconds
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);

    s = spec.tv_sec;
    ms = round(spec.tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds
    if (ms > 999) {
        s++;
        ms = 0;
    }

    printf("Current time: %" PRIdMAX ".%03ld seconds since the Epoch\n",
           (intmax_t)s, ms);
}

#define ADC_FREQ              (125.0)
#define MAKE_FREQ(freq, corr) ((uint32_t)floor((1.0 + 1.0e-6 * corr) * freq / ADC_FREQ * (1 << 30) + 0.5))

int main(int argc, char* argv[]) {
    int fd, offset, length, i, j;
    time_t t;
    struct tm* gmt;
    uint64_t* buffer;
    char date[12];
    char name[64];
    double dialfreq;
    double corr = 0.0;
    double freq[8] = { 1000.0, 20000.0, 300000.0, 400000.0, 500000.0, 600000.0, 700000.0, 800000.0 };
    int chan[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
    uint8_t value = 0;

    if ((fd = open("/dev/mem", O_RDWR)) < 0) {
        fprintf(stderr, "Cannot open /dev/mem.\n");
        return EXIT_FAILURE;
    }

    fpga_config = (FPGA_Config*)mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x40000000);
    fpga_status = (FPGA_Status*)mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, fd, 0x41000000);
    fpga_rx_data = (const int32_t*)mmap(NULL, 2 * sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, fd, 0x42000000);

    for (int i = 0; i < 4; i++) {
        fpga_wf_data[i] = (const uint32_t*)mmap(NULL, 1 * sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, fd, 0x43000000 + 0x01000000 * i);
    }

    fpga_pps_data = (const uint32_t*)mmap(NULL, 1 * sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, fd, 0x47000000);

    close(fd);

    fpga_config->reset = 0;


    fpga_config->reset = 0xff;
    for (i = 0; i < 8; ++i) {
        fpga_config->rx_freq[i] = MAKE_FREQ(freq[i], corr);
    }

    for (int i = 0; i < 4; i++) {
        fpga_config->wf_config[i].wf_freq = MAKE_FREQ(freq[i], corr);
        fpga_config->wf_config[i].wf_decim = 1 << (i + 3);
    }

    int wf_channels = (fpga_status->signature >> 8) & 0x0f;
    int rx_channels = (fpga_status->signature) & 0x0f;

    printf("DNA: %llx\n", fpga_status->fpga_dna);

    offset = 0;

    struct timespec start_spec, stop_spec;

    clock_gettime(CLOCK_REALTIME, &start_spec);

    printf("DNA: %llx\n", fpga_status->fpga_dna);
    //  printf("Count Addr&: %x\n", fpga_status->rx_fifo);
    printf("Signature: 0x%x\n", fpga_status->signature);

    int32_t tick, last_tick = 0;

    while (offset < 24000) {

        while (fpga_status->rx_fifo < 500) {
            // printf("Count: %d\n", fpga_status->rx_fifo);
            usleep(1000);
        }

        for (i = 0; i < 200; ++i) {
            tick = *fpga_rx_data;
            if (last_tick == 0)
                printf("get tick: %d\n", tick);
            else
                printf("tick diff: %d\n", tick - last_tick);
            last_tick = tick;
            for (j = 0; j < rx_channels; ++j) {
                int data0 = *fpga_rx_data;
                int data1 = *fpga_rx_data;
            }
        }

        offset += 200;
        // printf("%d\n", offset);
    }

    clock_gettime(CLOCK_REALTIME, &stop_spec);

    printf("RX: sample rate= %f (should close to 12,000, otherwise there is bug)\n", 24000.0 / (stop_spec.tv_sec - start_spec.tv_sec + (stop_spec.tv_nsec - start_spec.tv_nsec) * 1e-9));

    return EXIT_SUCCESS;
}
