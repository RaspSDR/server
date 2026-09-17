/*
 * Raw PCM frontend for the acarsdec decoder core.
 *
 * This program is distributed under the GNU General Public License version 2
 * to match the acarsdec sources it links with.
 */

#include "acars.h"
#include "acarsdec.h"
#include "label.h"
#include "msk.h"
#include "output.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

runtime_t R = {
    .mdly = 600,
};

static void stop_decoder(int signum)
{
    (void) signum;
    R.running = false;
}

char *parse_params(char **paramsp, struct params_s *sp, const int np)
{
    char *param;

    while ((param = strsep(paramsp, ","))) {
        char *sep = strchr(param, '=');
        if (!sep)
            return param;

        *sep++ = '\0';
        int i;
        for (i = 0; i < np; i++) {
            if (!strcmp(sp[i].name, param)) {
                *sp[i].valp = sep;
                break;
            }
        }

        if (i == np) {
            *--sep = '=';
            return param;
        }
    }

    return NULL;
}

static int decoder_init(void)
{
    char output_spec[] = "full:file:path=-";

    R.idstation = strdup("web-888");
    R.channels = calloc(1, sizeof(*R.channels));
    if (!R.idstation || !R.channels)
        return -1;

    R.nbch = 1;
    R.channels[0].chn = 0;
    R.channels[0].dm_buffer = malloc(DMBUFSZ * sizeof(*R.channels[0].dm_buffer));
    if (!R.channels[0].dm_buffer)
        return -1;

    build_label_filter(NULL);
    if (setup_output(output_spec) || initOutputs())
        return -1;
    if (initMsk(&R.channels[0]) || initAcars(&R.channels[0]))
        return -1;

    return 0;
}

int main(void)
{
    if (decoder_init()) {
        fprintf(stderr, "ACARS: decoder initialization failed\n");
        return 1;
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_decoder;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGQUIT, &action, NULL);

    R.running = true;
    fprintf(stderr, "ACARS: reading 12 kHz mono S16 PCM from stdin\n");

    int16_t pcm[DMBUFSZ];
    while (R.running) {
        size_t count = fread(pcm, sizeof(pcm[0]), ARRAY_SIZE(pcm), stdin);
        for (size_t i = 0; i < count; i++)
            R.channels[0].dm_buffer[i] = (float) pcm[i] / 65536.0f;
        if (count)
            demodMSK(&R.channels[0], count);
        if (count < ARRAY_SIZE(pcm)) {
            if (ferror(stdin) && errno == EINTR) {
                clearerr(stdin);
                continue;
            }
            break;
        }
    }

    R.running = false;
    deinitAcars();
    exitOutputs();
    return 0;
}
