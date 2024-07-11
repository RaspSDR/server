#pragma once

#include <linux/types.h>

/*-------------------------------------------------------------------------------------------------------------------------------------*/

#define AD8370_SET _IOW('Z', 0, __u32)

// 0 - HF Mode, 1 - Airmmode
#define MODE_SET _IOW('Z', 1, __u32)

// 0 - EXT CLK, 1 - INT CLK
#define CLK_SET _IOW('Z', 2, __u32)

// get FPGA DNA
#define GET_DNA _IOR('Z', 4, __u64)

// start RX, u16 is decimate
#define RX_START _IOW('Z', 5, __u32)

// set RX_parameter
struct rx_param_op {
    __u32 channel;
    __u64 freq;
} __attribute__((packed));
#define RX_PARAM _IOW('Z', 5, struct rx_param_op)

// read RX data
struct rx_read_op {
    __u32 address; // base address
    __u32 length;  // length in bytes
    __u32 readed;
} __attribute__((packed));
#define RX_READ _IOWR('Z', 6, struct rx_read_op)

// get/set GPIO mask
#define SET_GPIO_MASK _IOW('Z', 7, __u32)
#define GET_GPIO_MASK _IOR('Z', 8, __u32)

// get FPGA Signature
#define GET_SIGNATURE _IOR('Z', 9, __u32)

// _u32 low-bit is the channel, high bits are flag
#define WF_READ_CONTINUES 0x00010000

#define WF_START _IOW('Z', 10, __u32)

// set wf frequency and decimate
struct wf_param_op {
    __u16 channel;
    __u16 decimate;
    __u64 freq;
} __attribute__((packed));
#define WF_PARAM _IOW('Z', 11, struct wf_param_op)

// read wf data oneshot or continues
struct wf_read_op {
    __u16 channel;
    __u32 address; // base address
    __u32 length;  // length in bytes
    __u32 readed;
} __attribute__((packed));
#define WF_READ _IOWR('Z', 12, struct wf_read_op)

#define PPS_START _IOW('Z', 20, __u32)
// read PPS
#define PPS_READ _IOR('Z', 21, __u32)

/*-------------------------------------------------------------------------------------------------------------------------------------*/
