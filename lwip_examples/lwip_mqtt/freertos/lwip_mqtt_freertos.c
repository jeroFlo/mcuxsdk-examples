/*
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016-2024 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include "board.h"
#include "app.h"
#include "fsl_phy.h"
#include "fsl_gpio.h"
#include "fsl_io_mux.h"
#include "mqtt_freertos.h"

#include "lwip/opt.h"
#include "lwip/api.h"
#include "lwip/dhcp.h"
#include "lwip/netifapi.h"
#include "ethernetif.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/* Must be after include of app.h */
#ifndef configMAC_ADDR
#include "fsl_silicon_id.h"
#endif

#ifndef EXAMPLE_NETIF_INIT_FN
/*! @brief Network interface initialization function. */
#define EXAMPLE_NETIF_INIT_FN ethernetif0_init
#endif /* EXAMPLE_NETIF_INIT_FN */

/*! @brief Stack size of the temporary lwIP initialization thread. */
#define INIT_THREAD_STACKSIZE 1024

/*! @brief Priority of the temporary lwIP initialization thread. */
#define INIT_THREAD_PRIO DEFAULT_THREAD_PRIO

#define SWITCH_THREAD_STACKSIZE 512
#define SWITCH_THREAD_PRIO (DEFAULT_THREAD_PRIO + 1)
#define SWITCH_POLL_PERIOD_MS 20U
#define SWITCH_DEBOUNCE_MS 50U

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

static phy_handle_t phyHandle;
static bool lightState;

void mqtt_freertos_message_received(const char *topic, const char *message)
{
    if (strcmp(topic, EXAMPLE_MQTT_SUBSCRIBE_TOPIC) == 0)
    {
        if (strcmp(message, "1") == 0)
        {
            GPIO_PinWrite(GPIO, 0U, 1U, 0U);
            PRINTF("LED: ON\r\n");
        }
        else if (strcmp(message, "0") == 0)
        {
            GPIO_PinWrite(GPIO, 0U, 1U, 1U);
            PRINTF("LED: OFF\r\n");
        }
    }
}

static void switch_thread(void *arg)
{
    uint32_t stableState;
    uint32_t candidateState;

    LWIP_UNUSED_ARG(arg);

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

/*******************************************************************************
 * Code
 ******************************************************************************/

/*!
 * @brief Initializes lwIP stack.
 *
 * @param arg unused
 */
static void stack_init(void *arg)
{
    static struct netif netif;
    ethernetif_config_t enet_config = {
        .phyHandle   = &phyHandle,
        .phyAddr     = EXAMPLE_PHY_ADDRESS,
        .phyOps      = EXAMPLE_PHY_OPS,
        .phyResource = EXAMPLE_PHY_RESOURCE,
        .srcClockHz  = EXAMPLE_CLOCK_FREQ,
#ifdef configMAC_ADDR
        .macAddress = configMAC_ADDR,
#endif
    };

    LWIP_UNUSED_ARG(arg);

    /* Set MAC address. */
#ifndef configMAC_ADDR
    (void)SILICONID_ConvertToMacAddr(&enet_config.macAddress);
#endif

    tcpip_init(NULL, NULL);

    netifapi_netif_add(&netif, NULL, NULL, NULL, &enet_config, EXAMPLE_NETIF_INIT_FN, tcpip_input);
    netifapi_netif_set_default(&netif);
    netifapi_netif_set_up(&netif);

    netifapi_dhcp_start(&netif);

    PRINTF("\r\n************************************************\r\n");
    PRINTF(" MQTT client example\r\n");
    PRINTF("************************************************\r\n");

    while (ethernetif_wait_linkup(&netif, 5000) != ERR_OK)
    {
        PRINTF("PHY Auto-negotiation failed. Please check the cable connection and link partner setting.\r\n");
    }

    /* Wait for address from DHCP */

    PRINTF("Getting IP address from DHCP...\r\n");

    (void)ethernetif_wait_ipv4_valid(&netif, ETHERNETIF_WAIT_FOREVER);

    mqtt_freertos_run_thread(&netif);

    vTaskDelete(NULL);
}

/*!
 * @brief Main function
 */
int main(void)
{
    gpio_pin_config_t switchConfig = {kGPIO_DigitalInput, 0U};

    BOARD_InitHardware();
    IO_MUX_SetPinMux(IO_MUX_GPIO11);
    // IO_MUX_SetPinConfig(11U, IO_MUX_PinConfigPullUp);
    IO_MUX_SetPinMux(IO_MUX_GPIO1);
    GPIO_PortInit(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PORT);
    GPIO_PinInit(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PORT, BOARD_SW2_GPIO_PIN, &switchConfig);
    GPIO_PinInit(GPIO, 0U, 1U, &(gpio_pin_config_t){kGPIO_DigitalOutput, 0U});

    if (sys_thread_new("switch", switch_thread, NULL, SWITCH_THREAD_STACKSIZE, SWITCH_THREAD_PRIO) == NULL)
    {
        LWIP_ASSERT("main(): Switch task creation failed.", 0);
    }

    /* Initialize lwIP from thread */
    if (sys_thread_new("main", stack_init, NULL, INIT_THREAD_STACKSIZE, INIT_THREAD_PRIO) == NULL)
    {
        LWIP_ASSERT("main(): Task creation failed.", 0);
    }

    vTaskStartScheduler();

    /* Will not get here unless a task calls vTaskEndScheduler ()*/
    return 0;
}
