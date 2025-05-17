#pragma once

void mqtt_init(void);
bool mqtt_is_connected(void);
void mqtt_publish(const char* topic, const char* payload, ...);