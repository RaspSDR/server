#include "ext.h"
#include "kiwi.h"

#include <string.h>

#define SPACE_WEATHER_DEBUG_MSG	false

struct space_weather_t {
	u1_t rx_chan;
};

static space_weather_t space_weather[MAX_RX_CHANS];

bool space_weather_msgs(char *msg, int rx_chan)
{
	space_weather_t *e = &space_weather[rx_chan];

	if (strcmp(msg, "SET ext_server_init") == 0) {
		e->rx_chan = rx_chan;
		ext_send_msg(e->rx_chan, SPACE_WEATHER_DEBUG_MSG, "EXT ready");
		return true;
	}

	return false;
}

void space_weather_close(int rx_chan) {}

void space_weather_main();

ext_t space_weather_ext = {
	"space_weather",
	space_weather_main,
	space_weather_close,
	space_weather_msgs,
};

void space_weather_main()
{
	ext_register(&space_weather_ext);
}
