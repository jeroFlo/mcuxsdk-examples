/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_debug_console.h"
#include "fsl_silicon_id.h"
#include "fsl_enet.h"
#include "fsl_phy.h"
#include "board.h"
#include "app.h"
#include "mbedtls/gcm.h"

#include <string.h>

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define ENET_RXBD_NUM          (4)
#define ENET_TXBD_NUM          (4)
#define ENET_RXBUFF_SIZE       (ENET_FRAME_MAX_FRAMELEN)
#define ENET_TXBUFF_SIZE       (ENET_FRAME_MAX_FRAMELEN)
#define ENET_DATA_LENGTH       (1000)
#define ENET_TRANSMIT_DATA_NUM (20)
#define APP_USES_LOOPBACK_CABLE 1

#define ENET_HEADER_LENGTH     (14U)
#define AES_GCM_NONCE_LENGTH   (12U)
#define AES_GCM_TAG_LENGTH     (16U)
#define ENET_PAYLOAD_LENGTH    (ENET_DATA_LENGTH - ENET_HEADER_LENGTH)
#define ENET_PLAINTEXT_LENGTH  (ENET_PAYLOAD_LENGTH - AES_GCM_NONCE_LENGTH - AES_GCM_TAG_LENGTH)
static const uint8_t g_destinationMac[6] = {0x3cU, 0x18U, 0xa0U, 0x42U, 0x8cU, 0x64U};
/* @TEST_ANCHOR */

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
/*! @brief Build ENET unicast frame. */
static void ENET_BuildBroadCastFrame(void);

#if (defined(APP_PHY_LINK_INTR_SUPPORT) && (APP_PHY_LINK_INTR_SUPPORT))
void GPIO_EnableLinkIntr(void);
#endif

/*******************************************************************************
 * Variables
 ******************************************************************************/
/*! @brief Buffer descriptors should be in non-cacheable region and should be align to "ENET_BUFF_ALIGNMENT". */
AT_NONCACHEABLE_SECTION_ALIGN(enet_rx_bd_struct_t g_rxBuffDescrip[ENET_RXBD_NUM], ENET_BUFF_ALIGNMENT);
AT_NONCACHEABLE_SECTION_ALIGN(enet_tx_bd_struct_t g_txBuffDescrip[ENET_TXBD_NUM], ENET_BUFF_ALIGNMENT);
/*! @brief The data buffers can be in cacheable region or in non-cacheable region.
 * If use cacheable region, the alignment size should be the maximum size of "CACHE LINE SIZE" and "ENET_BUFF_ALIGNMENT"
 * If use non-cache region, the alignment size is the "ENET_BUFF_ALIGNMENT".
 */
SDK_ALIGN(uint8_t g_rxDataBuff[ENET_RXBD_NUM][SDK_SIZEALIGN(ENET_RXBUFF_SIZE, APP_ENET_BUFF_ALIGNMENT)],
          APP_ENET_BUFF_ALIGNMENT);
SDK_ALIGN(uint8_t g_txDataBuff[ENET_TXBD_NUM][SDK_SIZEALIGN(ENET_TXBUFF_SIZE, APP_ENET_BUFF_ALIGNMENT)],
          APP_ENET_BUFF_ALIGNMENT);

/*! @brief MAC transfer. */
static enet_handle_t g_handle;
static uint8_t g_frame[ENET_DATA_LENGTH];
static uint8_t g_plaintext[ENET_PLAINTEXT_LENGTH];
static uint8_t g_rxPlaintext[ENET_PLAINTEXT_LENGTH];
static uint32_t g_frameCounter;

/* Replace this example key with a provisioned key before deployment. */
static const uint8_t g_aesKey[16] = {
    0x10U, 0x32U, 0x54U, 0x76U, 0x98U, 0xBAU, 0xDCU, 0xFEU,
    0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xABU, 0xCDU, 0xEFU,
};

/*! @brief The MAC address for ENET device. */
#if APP_USER_DEFINED_MAC_ADDRESS
uint8_t g_macAddr[6] = {0x48, 0xea, 0x62, 0x99, 0x0b, 0xe3};
#else
uint8_t g_macAddr[6];
#endif

/*! @brief PHY status. */
static phy_handle_t phyHandle;
#if ((APP_USES_LOOPBACK_CABLE) && defined(APP_PHY_LINK_INTR_SUPPORT) && (APP_PHY_LINK_INTR_SUPPORT))
static bool linkChange = false;
#endif

static bool ENET_EncryptPayload(const uint8_t *plaintext)
{
    mbedtls_gcm_context gcm;
    uint8_t nonce[AES_GCM_NONCE_LENGTH] = {0};
    uint8_t *ciphertext = &g_frame[ENET_HEADER_LENGTH + AES_GCM_NONCE_LENGTH];
    uint8_t *tag = &ciphertext[ENET_PLAINTEXT_LENGTH];
    uint32_t deviceId = ((uint32_t)g_macAddr[2] << 24) | ((uint32_t)g_macAddr[3] << 16) |
                        ((uint32_t)g_macAddr[4] << 8) | g_macAddr[5];
    int result;

    memcpy(nonce, &deviceId, sizeof(deviceId));
    memcpy(&nonce[sizeof(deviceId)], &g_frameCounter, sizeof(g_frameCounter));
    memcpy(&g_frame[ENET_HEADER_LENGTH], nonce, sizeof(nonce));

    mbedtls_gcm_init(&gcm);
    result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, g_aesKey, 128U);
    if (result == 0)
    {
        result = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, ENET_PLAINTEXT_LENGTH, nonce,
                                           sizeof(nonce), g_frame, ENET_HEADER_LENGTH,
                                           plaintext, ciphertext,
                                           AES_GCM_TAG_LENGTH, tag);
    }
    mbedtls_gcm_free(&gcm);
    return result == 0;
}

static bool ENET_DecryptPayload(const uint8_t *frame, uint32_t frameLength, uint8_t *plaintext)
{
    mbedtls_gcm_context gcm;
    const uint8_t *nonce = &frame[ENET_HEADER_LENGTH];
    const uint8_t *ciphertext = &nonce[AES_GCM_NONCE_LENGTH];
    const uint8_t *tag = &ciphertext[ENET_PLAINTEXT_LENGTH];
    int result;

    if (frameLength != ENET_DATA_LENGTH)
    {
        return false;
    }

    mbedtls_gcm_init(&gcm);
    result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, g_aesKey, 128U);
    if (result == 0)
    {
        result = mbedtls_gcm_auth_decrypt(&gcm, ENET_PLAINTEXT_LENGTH, nonce, AES_GCM_NONCE_LENGTH,
                                          frame, ENET_HEADER_LENGTH, tag, AES_GCM_TAG_LENGTH, ciphertext,
                                          plaintext);
    }
    mbedtls_gcm_free(&gcm);
    return result == 0;
}

// static void ENET_PrintEncryptedPayload(void)
// {
//     uint32_t index;

//     PRINTF("Encrypted payload (nonce + ciphertext + tag), %u bytes:\r\n", ENET_PAYLOAD_LENGTH);
//     for (index = ENET_HEADER_LENGTH; index < ENET_DATA_LENGTH; index++)
//     {
//         PRINTF("%02x", g_frame[index]);
//         if (((index - ENET_HEADER_LENGTH + 1U) % 32U) == 0U)
//         {
//             PRINTF("\r\n");
//         }
//     }
//     if (((ENET_PAYLOAD_LENGTH) % 32U) != 0U)
//     {
//         PRINTF("\r\n");
//     }
// }

/*******************************************************************************
 * Code
 ******************************************************************************/
/*! @brief Build Frame for transmit. */
static void ENET_BuildBroadCastFrame(void)
{
    uint32_t count  = 0;
    uint32_t length = ENET_PAYLOAD_LENGTH;
    static const char message[] = "of course not, are you mad?";

    memcpy(g_frame, g_destinationMac, sizeof(g_destinationMac));
    memcpy(&g_frame[6], &g_macAddr[0], 6U);
    g_frame[12] = (length >> 8) & 0xFFU;
    g_frame[13] = length & 0xFFU;

    memcpy(g_plaintext, message, sizeof(message) - 1U);
    for (count = sizeof(message) - 1U; count < ENET_PLAINTEXT_LENGTH; count++)
    {
        g_plaintext[count] = count % 0xFFU;
    }

    if (!ENET_EncryptPayload(g_plaintext))
    {
        PRINTF("AES-GCM encryption failed.\r\n");
    }
    // else
    // {
    //     ENET_PrintEncryptedPayload();
    // }
}

#if (defined(APP_PHY_LINK_INTR_SUPPORT) && (APP_PHY_LINK_INTR_SUPPORT))
void PHY_LinkStatusChange(void)
{
#if (APP_USES_LOOPBACK_CABLE)
    linkChange = true;
#endif
}
#endif

/*!
 * @brief Main function
 */
int main(void)
{
    phy_config_t phyConfig = {0};
    uint32_t testTxNum     = 0;
    uint32_t length        = 0;
    enet_data_error_stats_t eErrStatic;
    status_t status;
    enet_config_t config;
#if APP_USES_LOOPBACK_CABLE
    volatile uint32_t count = 0;
    phy_speed_t speed;
    phy_duplex_t duplex;
    bool autonego = false;
    bool link     = false;
    bool tempLink = false;
#endif

    /* Hardware Initialization. */
    BOARD_InitHardware();

    PRINTF("MCUX SDK version: %s\r\n", MCUXSDK_VERSION_FULL_STR);

    PRINTF("\r\nENET example start.\r\n");

    /* Prepare the buffer configuration. */
    enet_buffer_config_t buffConfig[] = {{
        ENET_RXBD_NUM,
        ENET_TXBD_NUM,
        SDK_SIZEALIGN(ENET_RXBUFF_SIZE, APP_ENET_BUFF_ALIGNMENT),
        SDK_SIZEALIGN(ENET_TXBUFF_SIZE, APP_ENET_BUFF_ALIGNMENT),
        &g_rxBuffDescrip[0],
        &g_txBuffDescrip[0],
        &g_rxDataBuff[0][0],
        &g_txDataBuff[0][0],
        true,
        true,
        NULL,
    }};

    /* Get default configuration. */
    /*
     * config.miiMode = kENET_RmiiMode;
     * config.miiSpeed = kENET_MiiSpeed100M;
     * config.miiDuplex = kENET_MiiFullDuplex;
     * config.rxMaxFrameLen = ENET_FRAME_MAX_FRAMELEN;
     */
    ENET_GetDefaultConfig(&config);

    /* The miiMode should be set according to the different PHY interfaces. */
#ifdef EXAMPLE_PHY_INTERFACE_RGMII
    config.miiMode = kENET_RgmiiMode;
#else
    config.miiMode = kENET_RmiiMode;
#endif
    phyConfig.phyAddr = EXAMPLE_PHY_ADDRESS;
#if APP_USES_LOOPBACK_CABLE
    phyConfig.autoNeg = true;
#else
    phyConfig.autoNeg = false;
    config.miiDuplex  = kENET_MiiFullDuplex;
#endif
    phyConfig.ops      = EXAMPLE_PHY_OPS;
    phyConfig.resource = EXAMPLE_PHY_RESOURCE;
#if (defined(APP_PHY_LINK_INTR_SUPPORT) && (APP_PHY_LINK_INTR_SUPPORT))
    phyConfig.intrType = kPHY_IntrActiveLow;
#endif

    /* Initialize PHY and wait auto-negotiation over. */
    PRINTF("Wait for PHY init...\r\n");
#if APP_USES_LOOPBACK_CABLE
    do
    {
        status = PHY_Init(&phyHandle, &phyConfig);
        if (status == kStatus_Success)
        {
            PRINTF("Wait for PHY link up...\r\n");
            /* Wait for auto-negotiation success and link up */
            count = APP_PHY_AUTONEGO_TIMEOUT_COUNT;
            do
            {
                PHY_GetLinkStatus(&phyHandle, &link);
                if (link)
                {
                    PHY_GetAutoNegotiationStatus(&phyHandle, &autonego);
                    if (autonego)
                    {
                        break;
                    }
                }
            } while (--count);
            if (!autonego)
            {
                PRINTF("PHY Auto-negotiation failed. Please check the cable connection and link partner setting.\r\n");
            }
        }
    } while (!(link && autonego));
#else
    while (PHY_Init(&phyHandle, &phyConfig) != kStatus_Success)
    {
        PRINTF("PHY_Init failed\r\n");
    }

    /* set PHY link speed/duplex and enable loopback. */
    PHY_SetLinkSpeedDuplex(&phyHandle, (phy_speed_t)config.miiSpeed, (phy_duplex_t)config.miiDuplex);
    PHY_EnableLoopback(&phyHandle, kPHY_LocalLoop, (phy_speed_t)config.miiSpeed, true); //JROMEROF
#endif /* APP_USES_LOOPBACK_CABLE */

#if APP_PHY_STABILITY_DELAY_US
    /* Wait a moment for PHY status to be stable. */
    SDK_DelayAtLeastUs(APP_PHY_STABILITY_DELAY_US, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
#endif

#if APP_USES_LOOPBACK_CABLE
    /* Get the actual PHY link speed and set in MAC. */
    PHY_GetLinkSpeedDuplex(&phyHandle, &speed, &duplex);
    config.miiSpeed  = (enet_mii_speed_t)speed;
    config.miiDuplex = (enet_mii_duplex_t)duplex;
#endif

#if !APP_USER_DEFINED_MAC_ADDRESS
    /* Set special address for each chip. */
    SILICONID_ConvertToMacAddr(&g_macAddr);
#endif

    /* Init the ENET. */
    ENET_Init(EXAMPLE_ENET, &g_handle, &config, &buffConfig[0], &g_macAddr[0], EXAMPLE_CLOCK_FREQ);
    ENET_ActiveRead(EXAMPLE_ENET);

    /* Build broadcast for sending. */
    ENET_BuildBroadCastFrame();

    while (1)
    {
#if APP_USES_LOOPBACK_CABLE
        /* PHY link status update. */
#if (defined(APP_PHY_LINK_INTR_SUPPORT) && (APP_PHY_LINK_INTR_SUPPORT))
        if (linkChange)
        {
            linkChange = false;
            PHY_ClearInterrupt(&phyHandle);
            PHY_GetLinkStatus(&phyHandle, &link);
            GPIO_EnableLinkIntr();
        }
#else
        PHY_GetLinkStatus(&phyHandle, &link);
#endif
        if (tempLink != link)
        {
            PRINTF("PHY link changed, link status = %u\r\n", link);
            tempLink = link;
        }
#endif /*APP_USES_LOOPBACK_CABLE*/
        /* Get the Frame size */
        status = ENET_GetRxFrameSize(&g_handle, &length, 0);
        /* Call ENET_ReadFrame when there is a received frame. */
        if (length != 0)
        {
            /* Received valid frame. Deliver the rx buffer with the size equal to length. */
            uint8_t *data = (uint8_t *)malloc(length);
            status        = ENET_ReadFrame(EXAMPLE_ENET, &g_handle, data, length, 0, NULL);
            if (status == kStatus_Success)
            {
                if (ENET_DecryptPayload(data, length, g_rxPlaintext))
                {
                    PRINTF("AES-GCM frame received and authenticated. Payload: %c%c%c%c\r\n", g_rxPlaintext[0],
                           g_rxPlaintext[1], g_rxPlaintext[2], g_rxPlaintext[3]);
                }
                else
                {
                    PRINTF("Rejected frame: AES-GCM authentication failed.\r\n");
                }
            }
            free(data);
        }
        else if (status == kStatus_ENET_RxFrameError)
        {
            /* Update the received buffer when error happened. */
            /* Get the error information of the received g_frame. */
            ENET_GetRxErrBeforeReadFrame(&g_handle, &eErrStatic, 0);
            /* update the receive buffer. */
            ENET_ReadFrame(EXAMPLE_ENET, &g_handle, NULL, 0, 0, NULL);
        }

        if (testTxNum < ENET_TRANSMIT_DATA_NUM)
        {
            /* Send a multicast frame when the PHY is link up. */
#if APP_USES_LOOPBACK_CABLE
            if (link)
#endif
            {
                testTxNum++;
                g_frameCounter++;
                ENET_BuildBroadCastFrame();
                if (kStatus_Success ==
                    ENET_SendFrame(EXAMPLE_ENET, &g_handle, &g_frame[0], ENET_DATA_LENGTH, 0, false, NULL))
                {
                    PRINTF("The %d frame transmitted success!\r\n", testTxNum);
                }
                else
                {
                    PRINTF(" \r\nTransmit frame failed!\r\n");
                }
            }
        }
    }
}
