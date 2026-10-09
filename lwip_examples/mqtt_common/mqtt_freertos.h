/*
 * Copyright 2022 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MQTT_FREERTOS_H
#define MQTT_FREERTOS_H

#include "lwip/netif.h"
#include <stdbool.h>

/*! @brief MQTT server host name or IP address. */
#ifndef EXAMPLE_MQTT_SERVER_HOST
// #define EXAMPLE_MQTT_SERVER_HOST "test.mosquitto.org"
#define EXAMPLE_MQTT_SERVER_HOST "broker.hivemq.com"
#endif

/*! @brief MQTT server port number. */
#ifndef EXAMPLE_MQTT_SERVER_PORT
#define EXAMPLE_MQTT_SERVER_PORT 1883
#endif
// I AM TARJETA A
#ifndef EXAMPLE_MQTT_SUBSCRIBE_TOPIC
//#define EXAMPLE_MQTT_SUBSCRIBE_TOPIC "user/jeniferR1239/lucesclase/luzuj/data"
#define EXAMPLE_MQTT_SUBSCRIBE_TOPIC "equipo2/tarjeta_b/boton" 

#endif

#define LED_A_MQTT_PUBLISH_TOPIC "equipo2/tarjeta_a/led"


#ifndef EXAMPLE_MQTT_PUBLISH_TOPIC
#ifdef EXAMPLE_MQTT_PUBLISH_ON_SWITCH
// #define EXAMPLE_MQTT_PUBLISH_TOPIC "/user/jeniferR1239/lucesclase/luz34/data" // here goes Bricio's LED topic
#define EXAMPLE_MQTT_PUBLISH_TOPIC "equipo2/tarjeta_a/boton" 
#else
#define EXAMPLE_MQTT_PUBLISH_TOPIC "lwip_topic/100"
#endif
#endif

#ifndef EXAMPLE_MQTT_PUBLISH_MESSAGE
#ifdef EXAMPLE_MQTT_PUBLISH_ON_SWITCH
#define EXAMPLE_MQTT_PUBLISH_MESSAGE "1"
#else
#define EXAMPLE_MQTT_PUBLISH_MESSAGE "message from board"
#endif
#endif

/*!
 * @brief Create and run example thread
 *
 * @param netif  netif which example should use
 */
void mqtt_freertos_run_thread(struct netif *netif);

void mqtt_freertos_publish(const char *message);

void mqtt_freertos_publish_topic(const char *topic, const char *message);

void mqtt_freertos_message_received(const char *topic, const char *message);

#endif /* MQTT_FREERTOS_H */
