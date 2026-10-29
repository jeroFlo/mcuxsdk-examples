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
#include "lwip/api.h"
#include "lwip/apps/mdns.h"
#include "board.h"
#include "app.h"
#include "wpl.h"
#include "timers.h"

#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_io_mux.h"
#include "mqtt_freertos.h"
#include "cred_flash_storage.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define WIFI_NETWORK_LABEL "Home_1"
#define CONNECTION_INFO_FILENAME "connection_info.dat"
#define WIFI_SECURITY_WILDCARD_NAME "WILDCARD"
#define PROVISIONING_TCP_PORT 15000U
#define PROVISIONING_AP_SSID "SmartBulb1"
#define PROVISIONING_AP_PASSWORD "123carro$$$"
#define PROVISIONING_AP_CHANNEL 6U
#define PROVISIONING_MDNS_HOSTNAME "SmartBulb1_mdns"
#define PROVISIONING_MDNS_SERVICE "_smartbulb"
#define PROVISIONING_BUFFER_SIZE 128U

#define SWITCH_THREAD_STACKSIZE 512
#define SWITCH_THREAD_PRIORITY (configMAX_PRIORITIES - 3)
#define SWITCH_POLL_PERIOD_MS 20U
#define SWITCH_DEBOUNCE_MS 50U

/*******************************************************************************
 * Variables
 ******************************************************************************/

static bool lightState;

static char saved_ssid[WPL_WIFI_SSID_LENGTH + 1U];
static char saved_password[WPL_WIFI_PASSWORD_LENGTH + 1U];
static char saved_security[64U];

static struct netif *get_provisioning_netif(void)
{
    struct netif *netif;

    for (netif = netif_list; netif != NULL; netif = netif->next)
    {
        if (netif_is_up(netif) && !ip4_addr_isany_val(*netif_ip4_addr(netif)))
        {
            return netif;
        }
    }

    return NULL;
}

static void provisioning_mdns_name_result(struct netif *netif, u8_t result, s8_t service)
{
    LWIP_UNUSED_ARG(netif);
    LWIP_UNUSED_ARG(service);
    PRINTF("[i] mDNS announcement result: %u\r\n", result);
}

static void start_provisioning_mdns(void)
{
    struct netif *provisioning_netif = NULL;
    err_t error = ERR_OK;

    for (uint32_t attempt = 0U; attempt < 50U; attempt++)
    {
        LOCK_TCPIP_CORE();
        provisioning_netif = get_provisioning_netif();
        UNLOCK_TCPIP_CORE();
        if (provisioning_netif != NULL)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100U));
    }

    if (provisioning_netif == NULL)
    {
        PRINTF("[!] No active IPv4 interface for mDNS; TCP remains available\r\n");
        return;
    }

    LOCK_TCPIP_CORE();
    mdns_resp_register_name_result_cb(provisioning_mdns_name_result);
    mdns_resp_init();
    error = mdns_resp_add_netif(provisioning_netif, PROVISIONING_MDNS_HOSTNAME);
    if (error == ERR_OK)
    {
        error = mdns_resp_add_service(provisioning_netif, "SmartBulb1_jenifer", PROVISIONING_MDNS_SERVICE, DNSSD_PROTO_TCP,
                                      PROVISIONING_TCP_PORT, NULL, NULL);
    }
    if (error == ERR_OK)
    {
        mdns_resp_announce(provisioning_netif);
    }
    UNLOCK_TCPIP_CORE();

    if (error == ERR_OK)
    {
        PRINTF("[i] mDNS: SmartBulb1.%s._tcp.local -> %s:%u\r\n", PROVISIONING_MDNS_SERVICE,
               ip4addr_ntoa(netif_ip4_addr(provisioning_netif)), PROVISIONING_TCP_PORT);
    }
    else
    {
        PRINTF("[!] mDNS registration failed: %d; TCP remains available\r\n", error);
    }
}

static int32_t connect_to_saved_network(const char *ssid, const char *password)
{
    int32_t result = WPL_AddNetwork(ssid, password, WIFI_NETWORK_LABEL);

    // PRINTF("[i] Saved network profile result: %d\r\n", (int)result);

    if (result == WPLRET_SUCCESS)
    {
        PRINTF("[i] Connecting to saved network: %s\r\n", ssid);
        result = WPL_Join(WIFI_NETWORK_LABEL);
        // PRINTF("[i] Saved network join result: %d\r\n", (int)result);
    }

    if (result == WPLRET_SUCCESS)
    {
        char ip[16];
        WPL_GetIP(ip, 1);
        // PRINTF("[i] Connected using saved credentials; IPv4 address: %s\r\n", ip);
    }

    return result;
}

static bool parse_credentials(char *payload, char *ssid, char *password)
{
    char *separator;

    separator = strchr(payload, '\n');
    if (separator == NULL)
    {
        return false;
    }

    *separator = '\0';
    separator++;
    separator[strcspn(separator, "\r\n")] = '\0';

    if (payload[0] == '\0' || separator[0] == '\0')
    {
        return false;
    }

    strncpy(ssid, payload, 32U);
    ssid[32U] = '\0';
    strncpy(password, separator, 64U);
    password[64U] = '\0';
    return true;
}

static void provisioning_server_task(void *arg)
{
    struct netconn *listener;
    struct netconn *connection;
    struct netbuf *buffer;
    char payload[PROVISIONING_BUFFER_SIZE];
    char ssid[33];
    char password[65];
    void *data;
    u16_t length;
    u16_t payload_length;
    err_t error;

    LWIP_UNUSED_ARG(arg);
    listener = netconn_new(NETCONN_TCP);
    if (listener == NULL)
    {
        PRINTF("[!] Cannot create provisioning TCP socket\r\n");
        vTaskDelete(NULL);
        return;
    }

    error = netconn_bind(listener, IP_ADDR_ANY, PROVISIONING_TCP_PORT);
    if (error == ERR_OK)
    {
        error = netconn_listen(listener);
    }
    if (error != ERR_OK)
    {
        PRINTF("[!] Cannot listen on TCP port %u: %d\r\n", PROVISIONING_TCP_PORT, error);
        netconn_delete(listener);
        vTaskDelete(NULL);
        return;
    }

    PRINTF("[i] Provisioning server listening on TCP port %u\r\n", PROVISIONING_TCP_PORT);
    PRINTF("[i] Send: SSID\\nPASSWORD\\n\r\n");

    while ((error = netconn_accept(listener, &connection)) == ERR_OK)
    {
        PRINTF("[i] TCP client accepted\r\n");
        payload_length = 0U;
        error = netconn_recv(connection, &buffer);
        if (error == ERR_OK)
        {
            if (netbuf_data(buffer, &data, &length) == ERR_OK)
            {
                if ((size_t)payload_length + length < sizeof(payload))
                {
                    memcpy(&payload[payload_length], data, length);
                    payload_length += length;
                }
            }
            payload[payload_length] = '\0';
            netbuf_delete(buffer);

            PRINTF("[i] Received provisioning payload (%u bytes)\r\n", payload_length);

            if (parse_credentials(payload, ssid, password))
            {
                if (save_wifi_credentials(CONNECTION_INFO_FILENAME, ssid, password, WIFI_SECURITY_WILDCARD_NAME) != 0U)
                {
                    PRINTF("[!] Failed to persist Wi-Fi credentials\r\n");
                    netconn_write(connection, "ERROR: storage\n", 16U, NETCONN_COPY);
                    netconn_close(connection);
                    netconn_delete(connection);
                    continue;
                }

                /* A join can switch the radio away from the provisioning AP.
                 * Acknowledge and close TCP before rebooting into the saved config. */

                error = netconn_write(connection, "OK\n", 3U, NETCONN_COPY);
                if (error != ERR_OK)
                {
                    PRINTF("[!] Failed to send provisioning response: %d\r\n", error);
                }
                else
                {
                    PRINTF("[i] Provisioning response sent\r\n");
                }

                /* Finish the TCP exchange before changing the Wi-Fi interface. */
                vTaskDelay(pdMS_TO_TICKS(1000U));
                netconn_close(connection);
                netconn_delete(connection);
                netconn_close(listener);
                netconn_delete(listener);

                PRINTF("[i] Restarting to apply saved Wi-Fi credentials\r\n");
                NVIC_SystemReset();
                vTaskDelete(NULL);
                return;
            }

            PRINTF("[!] Invalid provisioning payload\r\n");
            error = netconn_write(connection, "ERROR: use SSID\\nPASSWORD\\n\n", 27U, NETCONN_COPY);
        }
        else
        {
            PRINTF("[!] TCP receive failed: %d\r\n", error);
        }
        netconn_close(connection);
        netconn_delete(connection);
    }

    PRINTF("[!] TCP accept failed: %d\r\n", error);
    netconn_delete(listener);
    vTaskDelete(NULL);
}

/*******************************************************************************
 * Code
 ******************************************************************************/

void mqtt_freertos_message_received(const char *topic, const char *message)
{
    if (strcmp(topic, EXAMPLE_MQTT_SUBSCRIBE_TOPIC) == 0)
    {
        if (strcmp(message, "1") == 0)
        {
            GPIO_PinWrite(GPIO, 0U, 12U, 0U);
            vTaskDelay(pdMS_TO_TICKS(1000U));
            if (GPIO_PinRead(GPIO, 0U, 12U) == 0U)
            {
                PRINTF("LED: ON\r\n");
                mqtt_freertos_publish_topic(LED_A_MQTT_PUBLISH_TOPIC, "1");
            }
        }
        else if (strcmp(message, "0") == 0)
        {
            GPIO_PinWrite(GPIO, 0U, 12U, 1U);
            vTaskDelay(pdMS_TO_TICKS(1000U));
            if (GPIO_PinRead(GPIO, 0U, 12U) == 1U)
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

/*!
 * @brief The main task function
 */
static void main_task(void *arg)
{
    uint32_t result = 0;
    uint32_t storage_result;

    PRINTF("\r\n************************************************\r\n");
    PRINTF(" Wi-Fi TCP provisioning\r\n");
    PRINTF("************************************************\r\n");

    storage_result = init_flash_storage(CONNECTION_INFO_FILENAME);
    if (storage_result != 0U)
    {
        PRINTF("[!] Flash credential storage initialization failed\r\n");
    }
    else
    {
        PRINTF("[i] Flash credential storage initialized: %s\r\n", CONNECTION_INFO_FILENAME);
    }

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

    if (storage_result == 0U)
    {
        uint32_t credentials_result =
            get_saved_wifi_credentials(CONNECTION_INFO_FILENAME, saved_ssid, saved_password, saved_security);

        if (credentials_result == 0U && saved_ssid[0] != '\0')
        {
            PRINTF("[i] Saved Wi-Fi credentials found for: %s\r\n", saved_ssid);
            // PRINTF("[i] Saved Wi-Fi password: %s\r\n", saved_password);
            if (connect_to_saved_network(saved_ssid, saved_password) == WPLRET_SUCCESS)
            {
                mqtt_freertos_run_thread(netif_default);
                vTaskDelete(NULL);
                return;
            }

            PRINTF("[!] Saved Wi-Fi connection failed; starting provisioning AP\r\n");
            WPL_RemoveNetwork(WIFI_NETWORK_LABEL);
        }
        else
        {
            PRINTF("[i] No saved Wi-Fi credentials found; starting provisioning AP\r\n");
        }
    }

    // Start provisioning AP and wait for credentials
    result = WPL_Start_AP(PROVISIONING_AP_SSID, PROVISIONING_AP_PASSWORD, PROVISIONING_AP_CHANNEL);
    if (result != WPLRET_SUCCESS)
    {
        PRINTF("[!] Failed to start provisioning AP: %d\r\n", (uint32_t)result);
        vTaskDelete(NULL);
        return;
    }

    {
        char ip[16];
        WPL_GetIP(ip, 0);
        PRINTF("[i] Join AP %s and connect to %s:%u\r\n", PROVISIONING_AP_SSID, ip, PROVISIONING_TCP_PORT);
    }

    start_provisioning_mdns();

    if (xTaskCreate(provisioning_server_task, "provision", 2048, NULL, configMAX_PRIORITIES - 3, NULL) != pdPASS)
    {
        PRINTF("[!] Provisioning server task creation failed\r\n");
        WPL_Stop_AP();
    }

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
    IO_MUX_SetPinMux(IO_MUX_GPIO12);
    GPIO_PortInit(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PORT);
    GPIO_PinInit(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PORT, BOARD_SW2_GPIO_PIN, &switchConfig);
    GPIO_PinInit(GPIO, 0U, 12U, &(gpio_pin_config_t){kGPIO_DigitalOutput, 0U});
    GPIO_PinWrite(GPIO, 0U, 12U, 1U);

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
