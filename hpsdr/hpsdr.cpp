#include "datatypes.h"
#include "config.h"
#include "kiwi.h"
#include "peri.h"
#include "clk.h"

#include <unistd.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct {
    uint16_t signature;
    uint8_t state;
    uint8_t mac[6];
    uint8_t major_version;
    uint8_t hardware_version;
    uint8_t padding[8];
    uint8_t num_of_channel;
    uint8_t padding_2[2];
} __attribute__((packed)) discovery_response_t;

static discovery_response_t discovery_response;
static int sock_ep2;
static int hpsdr_taskid;
static int ep6_taskid;

static int rx_rate;

static uint32_t rx_freqs[MAX_RX_CHANS];

static int receivers;

static void process_ep2(uint8_t* frame);
static void process_change_freq(int ch, uint32_t data);

static int active_thread = 0;
static int enable_thread = 0;

static struct sockaddr_in addr_ep6;

static void handler_ep6(void* arg);

static void hpsdr_task(void* arg) {
    while (1) {
        struct sockaddr_in addr_from;
        uint8_t buffer[1032];
        int size;
        socklen_t size_from;

        size_from = sizeof(addr_from);
        size = recvfrom(sock_ep2, buffer, 1032, 0, (struct sockaddr*)&addr_from, &size_from);
        if (size < 0) {
            perror("recvfrom");
            continue;
        }

        int code = *(uint32_t*)buffer;
        switch (code) {
        case 0x0201feef:
            process_ep2(buffer + 11);
            process_ep2(buffer + 523);
            break;

        case 0x0002feef:
            memcpy(buffer, &discovery_response, sizeof(discovery_response));
            memset(buffer + sizeof(discovery_response), 0, 63 - sizeof(discovery_response));
            buffer[2] = 2 + active_thread;
            sendto(sock_ep2, buffer, 63, 0, (struct sockaddr *)&addr_from, size_from);
            break;

        case 0x0004feef:
            fprintf(stderr, "SDR Program sends Stop command \n");
            enable_thread = 0;
            while (active_thread)
                usleep(1000);
            break;
        
        case 0x0104feef:
        case 0x0204feef:
        case 0x0304feef:
            fprintf(stderr, "Start Port %d \n", ntohs(addr_from.sin_port));
            enable_thread = 0;
            while (active_thread)
                usleep(1000);
            memset(&addr_ep6, 0, sizeof(addr_ep6));
            addr_ep6.sin_family = AF_INET;
            addr_ep6.sin_addr.s_addr = addr_from.sin_addr.s_addr;
            addr_ep6.sin_port = addr_from.sin_port;
            enable_thread = 1;
            active_thread = 1;
            ep6_taskid = CreateTask(handler_ep6, NULL, MAIN_PRIORITY);
            break;
        }
    }

    close(sock_ep2);
}

int hpsdr_init() {
#if 1
    memset(&discovery_response, 0, sizeof(discovery_response_t));

    discovery_response.signature = 0xFEEF;
    discovery_response.major_version = 73;
    discovery_response.hardware_version = 0x06;

    uint32_t signature = fpga_signature();
    discovery_response.num_of_channel = signature & 0x0f;

    if ((sock_ep2 = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        return -1;
    }

    struct ifreq hwaddr;
    memset(&hwaddr, 0, sizeof(hwaddr));
    strncpy(hwaddr.ifr_name, "eth0", IFNAMSIZ - 1);
    hwaddr.ifr_name[IFNAMSIZ - 1] = '\0'; // Null-terminate
    if (ioctl(sock_ep2, SIOCGIFHWADDR, &hwaddr) != -1) {
        for (int i = 0; i < 6; ++i)
            discovery_response.mac[i] = hwaddr.ifr_addr.sa_data[i];
    }

    int yes = 1;
    setsockopt(sock_ep2, SOL_SOCKET, SO_REUSEADDR, (void*)&yes, sizeof(yes));

    int optval = 7; // high priority.
    setsockopt(sock_ep2, SOL_SOCKET, SO_PRIORITY, &optval, sizeof(optval));

    optval = 65535;
    setsockopt(sock_ep2, SOL_SOCKET, SO_SNDBUF, (const char*)&optval, sizeof(optval));
    setsockopt(sock_ep2, SOL_SOCKET, SO_RCVBUF, (const char*)&optval, sizeof(optval));

    struct sockaddr_in addr_ep2;
    memset(&addr_ep2, 0, sizeof(addr_ep2));
    addr_ep2.sin_family = AF_INET;
    addr_ep2.sin_addr.s_addr = htonl(INADDR_ANY);
    addr_ep2.sin_port = htons(1024);

    if (bind(sock_ep2, (struct sockaddr*)&addr_ep2, sizeof(addr_ep2)) < 0) {
        perror("bind");
        return -1;
    }

    /* set default rx phase increment */
    for (int i = 0; i < discovery_response.num_of_channel; ++i)
    {
        rx_freqs[i] = 7100000;
        fpga_rxfreq(i, rx_freqs[i]);
    }

    hpsdr_taskid = CreateTask(hpsdr_task, 0, MAIN_PRIORITY);
#endif
    printf("HPSDR Protocol Handler Initialized111\n");

    return 0;
}

// https://github.com/softerhardware/Hermes-Lite2/wiki/Protocol
static void process_ep2(uint8_t* frame) {
    switch (frame[0] >> 1) {
    case 0x0: {
        receivers = ((frame[4] >> 3) & 7) + 1;

        /* set rx sample rate */
        switch (frame[1] & 3) {
        case 0:
            // 48khz
            if (rx_rate != 0) {
                rx_rate = 0;
                fpga_start_rx(48000);
            }
            break;
        case 1:
            // 96khz
            if (rx_rate != 1) {
                rx_rate = 1;
                fpga_start_rx(96000);
            }
            break;
        case 2:
            if (rx_rate != 2) {
                rx_rate = 2;
                fpga_start_rx(96000 * 2);
            }
            break;
        case 3:
            if (rx_rate != 3) {
                rx_rate = 3;
                fpga_start_rx(96000 * 4);
            }
            break;
        }

        break;
    }

    case 1:
        // Tx NCO frequency
        break;

    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
        /* set rx phase increment */
        process_change_freq((frame[0] >> 1) - 2, *(uint32_t*)(frame + 1));
        break;

    case 9:
        // LPF selection
        break;

    case 0x0a: {
        static int att_prev = -1;
        uint8_t att_cal = 0;
        if (frame[4] & 0x40) {
            /* set rx att*/
            att_cal = frame[4] & 0x3f;
        }
        else {
            // back compatibale mode
            if (frame[4] & 0x20)
                att_cal = (frame[4] & 0x1f) * 2;
            else
                att_cal = 0;
        }

        if (att_cal != att_prev) {
            att_prev = att_cal;
            printf("Input ATT is %d\n", att_cal);
            rf_attn_set((float)att_cal);
        }

        break;
    }

    default:
        printf("Unknown command: C0=%x C1=%x C2=%x C3=%x\n", frame[0] >> 1, frame[1], frame[2], frame[3]);
    }
}

static void process_change_freq(int ch, uint32_t data) {
    if (ch >= discovery_response.num_of_channel)
        return;

    uint64_t freq = ntohl(data);

     if (rx_freqs[ch] != freq) {
        fpga_rxfreq(ch, rx_freqs[ch]);
        fprintf(stderr, "Change channel %d to %d Hz\n", ch, freq);
    }
}

static const uint8_t headers[5][8] = {
  {127, 127, 127, 0, 0, 33, 17, 25,},
  {127, 127, 127, 8, 0, 0, 0, 0,},
  {127, 127, 127, 16, 0, 0, 0, 0,},
  {127, 127, 127, 24, 0, 0, 0, 0,},
  {127, 127, 127, 32, 66, 66, 66, 66}
};

static struct iovec iovec[25][1];
static struct mmsghdr datagram[25];

static void handler_ep6(void* arg)
{
  int i, j, n, m, size;
  int data_offset, header_offset;
  uint32_t counter;
  uint32_t data[6 * 4096];
  uint8_t buffer[25 * 1032];
  uint8_t *pointer;

  memset(iovec, 0, sizeof(iovec));
  memset(datagram, 0, sizeof(datagram));

  for (i = 0; i < 25; ++i)
  {
    *(uint32_t *)(buffer + i * 1032 + 0) = 0x0601feef;
    iovec[i][0].iov_base = buffer + i * 1032;
    iovec[i][0].iov_len = 1032;
    datagram[i].msg_hdr.msg_iov = iovec[i];
    datagram[i].msg_hdr.msg_iovlen = 1;
    datagram[i].msg_hdr.msg_name = &addr_ep6;
    datagram[i].msg_hdr.msg_namelen = sizeof(addr_ep6);
  }

  header_offset = 0;
  counter = 0;

  while (enable_thread)
  {
    size = receivers * 6 + 2;
    n = 504 / size;
    m = 128 / n;

    int samples = m * n;
    fpga_read_rx(data, sizeof(s4_t) * 2 * rx_chans * samples);

    data_offset = 0;
    for (i = 0; i < m; ++i)
    {
      *(uint32_t *)(buffer + i * 1032 + 4) = htonl(counter);
      ++counter;
    }

    for (i = 0; i < m * 2; ++i)
    {
      pointer = buffer + i * 516 - i % 2 * 4 + 8;
      memcpy(pointer, headers[header_offset], 8);
      header_offset = (header_offset + 1) % 5;

      pointer += 8;
      for (j = 0; j < n; ++j)
      {
        for(int k = 0; k < receivers; ++k)
        {
            // convert 32bit LSB to 24bit LSB
            int val = data[data_offset++] >> 8;
            pointer[i * 3] = (val >> 0) & 0xFF;
            pointer[i * 3 + 1] = (val >> 8) & 0xFF;
            pointer[i * 3 + 2] = (val >> 16) & 0xFF;
            pointer += 3;
            
            val = data[data_offset++] >> 8;
            pointer[i * 3] = (val >> 0) & 0xFF;
            pointer[i * 3 + 1] = (val >> 8) & 0xFF;
            pointer[i * 3 + 2] = (val >> 16) & 0xFF;
            pointer += 3;
        }

        data_offset += (rx_chans - receivers) * 2;
        pointer += 2; // mic data
      }

      pointer += 2;
    }

    sendmmsg(sock_ep2, datagram, m, 0);
  }

  active_thread = 0;
}
