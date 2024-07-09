
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <time.h>

#include "../si5351/LinuxInterface.h"
#include "../si5351/i2c.cpp"
#include "../si5351/si5351.cpp"
#include "../zynq/ioctl.h"

static const uint8_t buss_id = 0;
static const uint8_t chip_addr = 0x60;

int32_t buf[12000 * 2];

int main()
{
    struct timespec start_spec, stop_spec;

    printf("sysconf(_SC_PAGESIZE)=%ld\n", sysconf(_SC_PAGESIZE));
    // initialize si5351
    auto     i2c = new LinuxInterface(buss_id, chip_addr);
    auto si5351 = new Si5351(chip_addr, i2c);


    int ad8370_fd = open("/dev/zynqsdr", O_RDWR | O_SYNC);

    int int_clk = 1;
    ioctl(ad8370_fd, CLK_SET, &int_clk);

    bool i2c_found = si5351->init(SI5351_CRYSTAL_LOAD_0PF, 27000000, 0);

    int ret = si5351->set_freq((uint64_t)122880000* 100, SI5351_CLK0);
    printf("i2c si5351 initialized, error=%d\n", ret);

    // map config space
    int rc;
    uint64_t signature = 0;
    rc = ioctl(ad8370_fd, GET_DNA, &signature);
    printf("GET_DNA error %d\n", rc);
    
    printf("FPGA Bitstream signature: 0x%llx\n", signature);

    // start rx
    int decim = 122880000 / 12000 / 256;
    ioctl(ad8370_fd, RX_START, &decim);
    clock_gettime(CLOCK_REALTIME, &start_spec);

    rc = printf("RX_START[%d] error %d\n", decim, rc);

    struct rx_param_op param = {0, 122};
    rc = ioctl(ad8370_fd, RX_PARAM, &param);
    printf("RX_PARAM error %d errno=%d\n", rc, errno);

    memset(buf, 0, sizeof(buf));

    for(int i = 0; i < 100; i ++)
    {
        struct rx_read_op read_op = {(__u32)buf, sizeof(int32_t) * 2 * 12000};
        while(true)
        {
            //printf("In: 0x%x %d\t", read_op.address, read_op.length);
            rc = ioctl(ad8370_fd, RX_READ, &read_op);
            if (rc)
                break;
            //printf("OUT: 0x%x %d -> %d\n", read_op.address, read_op.length, read_op.readed);

            if (read_op.readed != read_op.length)
            {
                read_op.address += read_op.readed;
                read_op.length -= read_op.readed;
                usleep(1000);
            }
            else
            {
                break;
            }
        }

        if (rc < 0) {
            printf("rx read error %d, data=%d\n", rc, buf[0]);
            return -1;
        }

        int32_t min = INT32_MAX, max = -INT32_MAX;
        int zeros = 0;
        for(int i = 0; i < 12000 * 2; i++)
        {
            if (buf[i] == 0) zeros++;
            if (min > buf[i]) min = buf[i];
            if (max < buf[i]) max = buf[i];
        }

        printf("Zero count: %d , min=%08x max=%08X (%d)\n", zeros, min, max, min+max);
    }

    clock_gettime(CLOCK_REALTIME, &stop_spec);

    printf("\nRX: sample rate= %f (should close to 12,000, otherwise there is bug)\n", 12000.0 * 1000 / 13 / (stop_spec.tv_sec - start_spec.tv_sec + (stop_spec.tv_nsec - start_spec.tv_nsec) * 1e-9));

    // wf
    clock_gettime(CLOCK_REALTIME, &start_spec);

    wf_param_op wf_param = {0, 2, 122};
    rc = ioctl(ad8370_fd, WF_PARAM, &wf_param);
    if (rc < 0) {
        printf("WF set param failed.\n");
        return -1;
    }

    for(int i = 0; i < 100; i ++)
    {
        struct wf_read_op wf_read_op = {0, 0, (__u32)buf, sizeof(int32_t) * 8192};
        while(true)
        {
            printf("In: 0x%x %d\t", wf_read_op.address, wf_read_op.length);
            rc = ioctl(ad8370_fd, WF_READ, &wf_read_op);

            if (rc)
                break;
            printf("OUT: 0x%x %d -> %d\n", wf_read_op.address, wf_read_op.length, wf_read_op.readed);

            if (wf_read_op.readed != wf_read_op.length)
            {
                wf_read_op.address += wf_read_op.readed;
                wf_read_op.length -= wf_read_op.readed;
                usleep(100);
                continue;
            } else {
                break;
            }
        }

        int16_t *buf0 = (int16_t*)buf;
        int32_t min = INT32_MAX, max = -INT32_MAX;
        int zeros = 0;

        for(int i = 0; i < 8192 * 2; i++)
        {
            if (buf0[i] == 0) zeros++;
            if (min > buf0[i]) min = buf0[i];
            if (max < buf0[i]) max = buf0[i];
        }


        printf("Zero count: %d , min=%08x max=%08X (%d)\n", zeros, min, max, min+max);
    }

    clock_gettime(CLOCK_REALTIME, &stop_spec);

    printf("\nWF: sample rate= %f (should close to 12,000, otherwise there is bug)\n", 8192.0 * 100 / (stop_spec.tv_sec - start_spec.tv_sec + (stop_spec.tv_nsec - start_spec.tv_nsec) * 1e-9));

    close(ad8370_fd);

    return 0;
}