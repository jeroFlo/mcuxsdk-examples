/*
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016-2022 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include "lwip/tcpip.h"
#include "board.h"
#include "app.h"
#include "wpl.h"
#include "timers.h"

#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_io_mux.h"
#include "mqtt_freertos.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#ifndef AP_SSID
#define AP_SSID ""
#endif

#ifndef AP_PASSWORD
#define AP_PASSWORD ""
#endif

#define WIFI_NETWORK_LABEL "Home_1"

#define SWITCH_THREAD_STACKSIZE 512
#define SWITCH_THREAD_PRIORITY (configMAX_PRIORITIES - 3)
#define SWITCH_POLL_PERIOD_MS 20U
#define SWITCH_DEBOUNCE_MS 50U

/*******************************************************************************
 * Variables
 ******************************************************************************/

static bool lightState;

/*******************************************************************************
 * Code
 ******************************************************************************/

void mqtt_freertos_message_received(const char *topic, const char *message)
{
    if (strcmp(topic, EXAMPLE_MQTT_SUBSCRIBE_TOPIC) == 0)
    {
        if (strcmp(message, "1") == 0)
        {
            GPIO_PinWrite(GPIO, 0U, 1U, 0U);
            vTaskDelay(pdMS_TO_TICKS(1000U));
            if (GPIO_PinRead(GPIO, 0U, 1U) == 0U)
            {
                PRINTF("LED: ON\r\n");
                mqtt_freertos_publish_topic(LED_A_MQTT_PUBLISH_TOPIC, "1");
            }
        }
        else if (strcmp(message, "0") == 0)
        {
            GPIO_PinWrite(GPIO, 0U, 1U, 1U);
            vTaskDelay(pdMS_TO_TICKS(1000U));
            if (GPIO_PinRead(GPIO, 0U, 1U) == 1U)
            {
                PRINTF("LED: OFF\r\n");
                mqtt_freertos_publish_topic(LED_A_MQTT_PUBLISH_TOPIC, "0");
            }
        }
    }
}

static void switch_task(void *arg)
{
    uint32_t stableState;
    uint32_t candidateState;

    (void)arg;

    stableState = GPIO_PinRead(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PORT, BOARD_SW2_GPIO_PIN);

    while (1)
    {
        candidateState = GPIO_PinRead(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PORT, BOARD_SW2_GPIO_PIN);

        if (candidateState != stableState)
        {
            vTaskDelay(pdMS_TO_TICKS(SWITCH_DEBOUNCE_MS));

            if (GPIO_PinRead(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PORT, BOARD_SW2_GPIO_PIN) == candidateState)
            {
                stableState = candidateState;

                if (stableState == 0U)
                {
                    lightState = !lightState;
                    mqtt_freertos_publish(lightState ? "1" : "0");
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SWITCH_POLL_PERIOD_MS));
    }
}

/* Link lost callback */
static void LinkStatusChangeCallback(bool linkState)
{
    if (linkState == false)
    {
        /* -------- LINK LOST -------- */
        /* DO SOMETHING */
        PRINTF("-------- LINK LOST --------\r\n");
    }
    else
    {
        /* -------- LINK REESTABLISHED -------- */
        /* DO SOMETHING */
        PRINTF("-------- LINK REESTABLISHED --------\r\n");
    }
}

/* Connect to the external AP */
static void ConnectTo()
{
    int32_t result;

    /* Add Wi-Fi network */
    result = WPL_AddNetwork(AP_SSID, AP_PASSWORD, WIFI_NETWORK_LABEL);
    if (result == WPLRET_SUCCESS)
    {
        PRINTF("Connecting as client to ssid: %s \r\n", AP_SSID);
        result = WPL_Join(WIFI_NETWORK_LABEL);
    }

    if (result != WPLRET_SUCCESS)
    {
        PRINTF("[!] Cannot connect to Wi-Fi\r\n[!]ssid: %s\r\n[!]passphrase: %s\r\n", AP_SSID, AP_PASSWORD);

        while (1)
        {
            __BKPT(0);
        }
    }
    else
    {
        PRINTF("[i] Connected to Wi-Fi\r\nssid: %s\r\n[!]passphrase: %s\r\n", AP_SSID, AP_PASSWORD);
        char ip[16];
        WPL_GetIP(ip, 1);
    }
}

/*!
 * @brief The main task function
 */
static void main_task(void *arg)
{
    uint32_t result = 0;

    PRINTF("\r\n************************************************\r\n");
    PRINTF(" MQTT client example\r\n");
    PRINTF("************************************************\r\n");

    /* Initialize Wi-Fi board */
    PRINTF("[i] Initializing Wi-Fi connection... \r\n");

    result = WPL_Init();
    if (result != WPLRET_SUCCESS)
    {
        PRINTF("[!] WPL Init failed: %d\r\n", (uint32_t)result);
        __BKPT(0);
    }

    result = WPL_Start(LinkStatusChangeCallback);
    if (result != WPLRET_SUCCESS)
    {
        PRINTF("[!] WPL Start failed %d\r\n", (uint32_t)result);
        __BKPT(0);
    }

    PRINTF("[i] Successfully initialized Wi-Fi module\r\n");

    ConnectTo();
    /// wait_dns

    mqtt_freertos_run_thread(netif_default);

    vTaskDelete(NULL);
}

/*!
 * @brief Main function.
 */
int main(void)
{
    gpio_pin_config_t switchConfig = {kGPIO_DigitalInput, 0U};

    /* Initialize the hardware */
    BOARD_InitHardware();

    IO_MUX_SetPinMux(IO_MUX_GPIO11);
    IO_MUX_SetPinMux(IO_MUX_GPIO1);
    GPIO_PortInit(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PORT);
    GPIO_PinInit(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PORT, BOARD_SW2_GPIO_PIN, &switchConfig);
    GPIO_PinInit(GPIO, 0U, 1U, &(gpio_pin_config_t){kGPIO_DigitalOutput, 0U});
    GPIO_PinWrite(GPIO, 0U, 1U, 1U);

    if (xTaskCreate(switch_task, "switch", SWITCH_THREAD_STACKSIZE, NULL, SWITCH_THREAD_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("[!] Switch task creation failed!\r\n");
        while (1)
            ;
    }

    /* Create the main Task */
    if (xTaskCreate(main_task, "main_task", 1024, NULL, configMAX_PRIORITIES - 4, NULL) != pdPASS)
    {
        PRINTF("[!] MAIN Task creation failed!\r\n");
        while (1)
            ;
    }

    /* Run RTOS */
    vTaskStartScheduler();

    /* Should not reach this statement */
    for (;;)
        ;
}
