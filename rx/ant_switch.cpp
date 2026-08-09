// Copyright (c) 2018-2023 Kari Karvonen, OH1KK
// Ported from extension to core component for Web-888

#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "conn.h"
#include "rx.h"
#include "rx_cmd.h"
#include "rx_util.h"
#include "cfg.h"
#include "str.h"
#include "peri.h"
#include "misc.h"
#include "printf.h"
#include "web.h"
#include "ant_switch.h"

#include <stdio.h>
#include <string.h>

#define ANT_SWITCH_DEBUG_MSG false

static int ver_maj, ver_min, n_ch;
static uint8_t antenna_current;

void ant_switch_init()
{
    ver_maj = 2;
    ver_min = 1;
    n_ch = 6;
    antenna_current = 0;
    have_ant_switch_ext = true;
    printf("ant_switch: version %d.%d channels=%d\n", ver_maj, ver_min, n_ch);
    kiwi.ant_switch_nch = n_ch;
}

static void ant_switch_setantenna(int antenna)
{
    if (antenna == 0) {
        antenna_current = 0;
    } else {
        antenna_current = 1 << (antenna - 1);
    }
    fpga_set_antenna(antenna_current);
}

static void ant_switch_toggleantenna(int antenna)
{
    if (antenna == 0) {
        antenna_current = 0;
    } else {
        antenna--;
        if (antenna_current & (1 << antenna))
            antenna_current &= ~(1 << antenna);
        else
            antenna_current |= 1 << antenna;
    }
    fpga_set_antenna(antenna_current);
}

static bool ant_switch_validate_cmd(int cmd)
{
    return (cmd >= 0 && cmd <= n_ch);
}

static bool ant_switch_read_denyswitching(conn_t *conn)
{
    bool error;
    int deny_val = cfg_int("ant_switch.denyswitching", &error, CFG_OPTIONAL);
    #define ALLOW_EVERYONE 0
    #define ALLOW_LOCAL_ONLY 1
    #define ALLOW_LOCAL_OR_PASSWORD_ONLY 2
    if (error) deny_val = ALLOW_EVERYONE;
    if (conn->isLocal) return false;
    if (deny_val == ALLOW_LOCAL_ONLY) return true;
    if (deny_val == ALLOW_LOCAL_OR_PASSWORD_ONLY && !conn->tlimit_exempt_by_pwd) return true;
    return false;
}

static bool ant_switch_read_denymixing()
{
    return (cfg_int("ant_switch.denymixing", NULL, CFG_OPTIONAL) == 1);
}

static bool ant_switch_read_denymultiuser(conn_t *conn)
{
    bool error;
    int deny = cfg_int("ant_switch.denymultiuser", &error, CFG_OPTIONAL);
    if (error) deny = 0;
    if (conn->isLocal) deny = false;
    return (deny && current_nusers > 1);
}

static bool ant_switch_read_thunderstorm()
{
    return (cfg_int("ant_switch.thunderstorm", NULL, CFG_OPTIONAL) == 1);
}

bool ant_switch_msgs(char *msg, conn_t *conn)
{
    int n = 0;
    int antenna;

    n = sscanf(msg, "SET antsw_Antenna=%d", &antenna);
    if (n == 1) {
        int deny_reason = 0;
        if (ant_switch_read_denyswitching(conn))
            deny_reason = 1;
        else if (ant_switch_read_denymultiuser(conn))
            deny_reason = 2;
        send_msg(conn, SM_NO_DEBUG, "MSG antsw_AntennaDenySwitching=%d", deny_reason);
        if (deny_reason != 0) return true;

        if (ant_switch_validate_cmd(antenna)) {
            if (ant_switch_read_denymixing())
                ant_switch_setantenna(antenna);
            else
                ant_switch_toggleantenna(antenna);
        } else {
            printf("ant_switch: invalid SET antsw_Antenna=%d\n", antenna);
        }
        return true;
    }

    if (strcmp(msg, "SET antsw_GetAntenna") == 0) {
        char buf[32] = "";
        if (antenna_current == 0) {
            snprintf(buf, sizeof(buf), "0");
        } else {
            for (int i = 0; i < n_ch; i++) {
                if (antenna_current & (1 << i))
                    sprintf(buf + strlen(buf), "%d,", i + 1);
            }
            buf[strlen(buf) - 1] = '\0';
        }

        send_msg(conn, SM_NO_DEBUG, "MSG antsw_Antenna=%s", buf);

        int deny_reason = 0;
        if (ant_switch_read_denyswitching(conn))
            deny_reason = 1;
        else if (ant_switch_read_denymultiuser(conn))
            deny_reason = 2;
        send_msg(conn, SM_NO_DEBUG, "MSG antsw_AntennaDenySwitching=%d", deny_reason);
        send_msg(conn, SM_NO_DEBUG, "MSG antsw_AntennaDenyMixing=%d", ant_switch_read_denymixing() ? 1 : 0);

        if (ant_switch_read_thunderstorm()) {
            send_msg(conn, SM_NO_DEBUG, "MSG antsw_Thunderstorm=1");
            if (antenna_current == 0) {
                ant_switch_setantenna(0);
            }
        } else {
            send_msg(conn, SM_NO_DEBUG, "MSG antsw_Thunderstorm=0");
        }
        return true;
    }

    int freq_offset_ant;
    n = sscanf(msg, "SET antsw_freq_offset=%d", &freq_offset_ant);
    if (n == 1) {
        cfg_set_float_save("freq_offset", (double)freq_offset_ant);
        freq_offset = freq_offset_ant;
        return true;
    }

    int high_side_ant;
    n = sscanf(msg, "SET antsw_high_side=%d", &high_side_ant);
    if (n == 1) {
        kiwi.spectral_inversion_lockout = true;
        kiwi.spectral_inversion = high_side_ant ? true : false;
        return true;
    }

    if (strcmp(msg, "SET antsw_init") == 0) {
        send_msg(conn, SM_NO_DEBUG, "MSG antsw_ready");
        send_msg(conn, ANT_SWITCH_DEBUG_MSG, "MSG antsw_backend_ver=%d.%d", ver_maj, ver_min);
        send_msg(conn, ANT_SWITCH_DEBUG_MSG, "MSG antsw_channels=%d", n_ch);
        return true;
    }

    return false;
}
