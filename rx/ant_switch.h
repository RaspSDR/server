#pragma once

#include "types.h"
#include "conn.h"

void ant_switch_init();
bool ant_switch_msgs(char *msg, conn_t *conn);
