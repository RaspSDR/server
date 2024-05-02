// Copyright (c) 2018-2023 Kari Karvonen, OH1KK

#include "ext.h" // all calls to the extension interface begin with "ext_", e.g. ext_register()

#include "kiwi.h"
#include "cfg.h"
#include "str.h"
#include "peri.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>

// #define ANT_SWITCH_DEBUG_MSG	true
#define ANT_SWITCH_DEBUG_MSG false

static int ver_maj, ver_min, n_ch;

static void ant_backend_info()
{
#if 0
    char *cmd, *reply;
    int n;
    asprintf(&cmd, "/root/extensions/ant_switch/frontend/ant-switch-frontend bi");
    reply = non_blocking_cmd(cmd, NULL);
    free(cmd);
    char *sp = kstr_sp(reply);
    n = sscanf(sp, "%*s version %d.%d %d", &ver_maj, &ver_min, &n_ch);
    char *space = index(sp, ' ');
    if (space)
        *space = '\0';
    printf("ant_switch backend info: version %d.%d channels=%d %s\n", ver_maj, ver_min, n_ch, sp);
    kstr_free(reply);
    kiwi.ant_switch_nch = n_ch;
#else
    kiwi.ant_switch_nch = 5;
#endif
}

static void ant_switch_init(int rx_chan)
{
    ext_send_msg(rx_chan, ANT_SWITCH_DEBUG_MSG, "EXT backend_ver=%d.%d", ver_maj, ver_min);
    ext_send_msg(rx_chan, ANT_SWITCH_DEBUG_MSG, "EXT channels=%d", n_ch);
}

char *ant_switch_queryantennas()
{
    static char selected_antennas[64 + SPACE_FOR_NULL] = "gpio";
#if 0
    char *cmd, *reply;
    int n;
    asprintf(&cmd, "/root/extensions/ant_switch/frontend/ant-switch-frontend s");
    reply = non_blocking_cmd(cmd, NULL);
    n = sscanf(kstr_sp(reply), "Selected antennas: %64s", selected_antennas);
    free(cmd);
    // printf("ant_switch frontend: s n=%d reply=<%s>\n", n, kstr_sp(reply));
    if (!n)
        printf("ant_switch_queryantenna BAD STATUS? <%s>\n", kstr_sp(reply));
    kstr_free(reply);
#endif
    return (selected_antennas);
}

int ant_switch_setantenna(char *antenna)
{
#if 0
    char *cmd, *reply;
    int status;
    int n;
    asprintf(&cmd, "/root/extensions/ant_switch/frontend/ant-switch-frontend %s", antenna);
    // printf("ant_switch frontend: %s\n", antenna);
    reply = non_blocking_cmd(cmd, NULL);
    free(cmd);
    kstr_free(reply);
#endif
    return (0);
}

int ant_switch_toggleantenna(char *antenna)
{
#if 0
    char *cmd, *reply;
    int status;
    int n;
    asprintf(&cmd, "/root/extensions/ant_switch/frontend/ant-switch-frontend t%s", antenna);
    // printf("ant_switch frontend: t%s\n", antenna);
    reply = non_blocking_cmd(cmd, NULL);
    free(cmd);
    kstr_free(reply);
#endif
    return (0);
}

int ant_switch_validate_cmd(char *cmd)
{
    return (cmd[0] >= '1' && cmd[0] <= '9');
}

bool ant_switch_read_denyswitching(int rx_chan)
{
    bool deny = false;
    bool error;
    int deny_val = cfg_int("ant_switch.denyswitching", &error, CFG_OPTIONAL);

// values used by admin menu
#define ALLOW_EVERYONE 0
#define ALLOW_LOCAL_ONLY 1
#define ALLOW_LOCAL_OR_PASSWORD_ONLY 2

    if (error)
        deny_val = ALLOW_EVERYONE;
    ext_auth_e auth = ext_auth(rx_chan);
    if (deny_val == ALLOW_LOCAL_ONLY && auth != AUTH_LOCAL)
        deny = true;
    if (deny_val == ALLOW_LOCAL_OR_PASSWORD_ONLY && auth == AUTH_USER)
        deny = true;
    // rcprintf(rx_chan, "ant_switch: deny_val=%d auth=%d => deny=%d\n", deny_val, auth, deny);

    return (deny) ? true : false;
}

bool ant_switch_read_denymixing()
{
    int result = cfg_int("ant_switch.denymixing", NULL, CFG_OPTIONAL);
    // error handling: if deny parameter is not defined, or it is 0, then mixing is allowed
    if (result == 1)
        return true;
    else
        return false;
}

bool ant_switch_read_denymultiuser(int rx_chan)
{
    bool error;
    int deny = cfg_int("ant_switch.denymultiuser", &error, CFG_OPTIONAL);
    if (error)
        deny = 0;

#if (VERSION_MAJ > 1) || (VERSION_MAJ == 1 && VERSION_MIN >= 448)
    if (ext_auth(rx_chan) == AUTH_LOCAL)
        deny = false; // don't apply to local connections
#endif

    return (deny && current_nusers > 1) ? true : false;
}

bool ant_switch_read_thunderstorm()
{
    int result = cfg_int("ant_switch.thunderstorm", NULL, CFG_OPTIONAL);
    // error handling: if deny parameter is not defined, or it is 0, then mixing is allowed
    if (result == 1)
        return true;
    else
        return false;
}

bool ant_switch_msgs(char *msg, int rx_chan)
{
    int n = 0;
    char antenna[256];

    // rcprintf(rx_chan, "### ant_switch_msgs <%s>\n", msg);

    if (strcmp(msg, "SET ext_server_init") == 0)
    {
        ext_send_msg(rx_chan, ANT_SWITCH_DEBUG_MSG, "EXT ready");
        ant_switch_init(rx_chan);
        return true;
    }

    n = sscanf(msg, "SET Antenna=%s", antenna);
    if (n == 1)
    {
        // rcprintf(rx_chan, "ant_switch: %s\n", msg);
        int deny_reason = 0;
        if (ant_switch_read_denyswitching(rx_chan) == true)
            deny_reason = 1;
        else if (ant_switch_read_denymultiuser(rx_chan) == true)
            deny_reason = 2;
        ext_send_msg(rx_chan, ANT_SWITCH_DEBUG_MSG, "EXT AntennaDenySwitching=%d", deny_reason);
        if (deny_reason != 0)
            return true;

        if (ant_switch_validate_cmd(antenna))
        {
            if (ant_switch_read_denymixing() == 1)
            {
                ant_switch_setantenna(antenna);
            }
            else
            {
                ant_switch_toggleantenna(antenna);
            }
        }
        else
        {
            rcprintf(rx_chan, "ant_switch: Command not valid SET Antenna=%s", antenna);
        }

        return true;
    }

    if (strcmp(msg, "GET Antenna") == 0)
    {
        char *selected_antennas = ant_switch_queryantennas();
        ext_send_msg(rx_chan, ANT_SWITCH_DEBUG_MSG, "EXT Antenna=%s", selected_antennas);

// setup user notification of antenna change
#if (VERSION_MAJ > 1) || (VERSION_MAJ == 1 && VERSION_MIN >= 449)
        static char last_selected_antennas[64];
        if (strncmp(selected_antennas, last_selected_antennas, 64))
        {
            char *s;
            if (strcmp(selected_antennas, "g") == 0)
                s = (char *)"All antennas now grounded.";
            else
                s = stprintf("Selected antennas are now: %s", selected_antennas);
            static u4_t seq;
            ext_notify_connected(rx_chan, seq++, s);
            kiwi_strncpy(last_selected_antennas, selected_antennas, 64);
        }
#endif

        int deny_reason = 0;
        if (ant_switch_read_denyswitching(rx_chan) == true)
            deny_reason = 1;
        else if (ant_switch_read_denymultiuser(rx_chan) == true)
            deny_reason = 2;
        ext_send_msg(rx_chan, ANT_SWITCH_DEBUG_MSG, "EXT AntennaDenySwitching=%d", deny_reason);

        if (ant_switch_read_denymixing() == true)
        {
            ext_send_msg(rx_chan, ANT_SWITCH_DEBUG_MSG, "EXT AntennaDenyMixing=1");
        }
        else
        {
            ext_send_msg(rx_chan, ANT_SWITCH_DEBUG_MSG, "EXT AntennaDenyMixing=0");
        }

        if (ant_switch_read_thunderstorm() == true)
        {
            ext_send_msg(rx_chan, ANT_SWITCH_DEBUG_MSG, "EXT Thunderstorm=1");
            // also ground antenna if not grounded
            if (strcmp(selected_antennas, "g") != 0)
            {
                char *groundall = (char *)"g\0";
                ant_switch_setantenna(groundall);
                ext_send_msg(rx_chan, ANT_SWITCH_DEBUG_MSG, "EXT Antenna=g");
            }
            return true;
        }
        else
        {
            ext_send_msg(rx_chan, ANT_SWITCH_DEBUG_MSG, "EXT Thunderstorm=0");
        }

        return true;
    }

    int freq_offset_ant;
    n = sscanf(msg, "SET freq_offset=%d", &freq_offset_ant);
    if (n == 1)
    {
        // rcprintf(rx_chan, "ant_switch: freq_offset %d\n", freq_offset_ant);
        cfg_set_float_save("freq_offset", (double)freq_offset_ant);
        freq_offset = freq_offset_ant;
        return true;
    }

    int high_side_ant;
    n = sscanf(msg, "SET high_side=%d", &high_side_ant);
    if (n == 1)
    {
        // rcprintf(rx_chan, "ant_switch: high_side %d\n", high_side_ant);
        //  if antenna switch extension is active override current inversion setting
        //  and lockout the admin config page setting until a restart
        kiwi.spectral_inversion_lockout = true;
        kiwi.spectral_inversion = high_side_ant ? true : false;
        return true;
    }

    return false;
}

void ant_switch_close(int rx_chan)
{
    // do nothing
}

void ant_switch_main();

ext_t ant_switch_ext = {
    "ant_switch",
    ant_switch_main,
    ant_switch_close,
    ant_switch_msgs,
};

void ant_switch_main()
{
    ext_register(&ant_switch_ext);

    ant_backend_info();
}