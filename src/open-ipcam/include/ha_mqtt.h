#ifndef HA_MQTT_H
#define HA_MQTT_H

#include "common.h"

int ha_mqtt_init(void);
void ha_mqtt_stop(void);
void *ha_mqtt_thread(void *arg);

int ha_mqtt_publish_discovery(void);
int ha_mqtt_publish_state(void);

#endif // HA_MQTT_H
