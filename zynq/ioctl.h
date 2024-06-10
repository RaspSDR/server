#pragma once

#include <linux/types.h>

#define AD8370_SET _IOW('Z', 0, __u32)

// 0 - HF Mode, 1 - Airmmode
#define MODE_SET _IOW('Z', 1, __u32)

// 0 - EXT CLK, 1 - INT CLK
#define CLK_SET  _IOW('Z', 2, __u32)

// get FPGA DNA
#define GET_DNA _IOR('Z', 4, __u64)

struct reset_cmd_op {
    __u8 device;
    bool reset;
};
#define RX_RESET _IOW('Z', 5, struct reset_cmd_op)

struct rx_cmd_op {
    __u64 frequency;
    __u16 length;
    void *buffer;
};
#define RX_READ _IOW('Z', 6, struct rx_cmd_op)

struct wf_cmd_op {
    __u8 channel;
    __u8 decimate;
    __u64 frequency;
    __u16 length;
    void *buffer;
};
#define WF_READ _IOW('Z', 7, struct wf_cmd_op)