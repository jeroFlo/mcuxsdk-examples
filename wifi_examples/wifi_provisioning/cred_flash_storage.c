/*
 * Copyright 2020 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "cred_flash_storage.h"
#include "fsl_debug_console.h"
#include "mflash_file.h"
#include "wpl.h"

#include <stdio.h>
#include <string.h>

#define FILE_HEADER "wifi_credentials:"
#define SECURITY_LENGTH 63U

uint32_t init_flash_storage(char *filename)
{
    mflash_file_t file_table[] = {{.path = filename, .max_size = 200}, {0}};

    if (mflash_init(file_table, 1) != kStatus_Success)
    {
        PRINTF("[!] ERROR in mflash_init!\r\n");
        return 1;
    }

    return 0;
}

uint32_t save_wifi_credentials(char *filename, char *ssid, char *passphrase, char *security)
{
    char credentials_buf[sizeof(FILE_HEADER) + WPL_WIFI_SSID_LENGTH + WPL_WIFI_PASSWORD_LENGTH + SECURITY_LENGTH + 4U];

    if (filename == NULL || ssid == NULL || passphrase == NULL || security == NULL || strlen(filename) > 63U ||
        strlen(ssid) > WPL_WIFI_SSID_LENGTH || strlen(passphrase) > WPL_WIFI_PASSWORD_LENGTH ||
        strlen(security) > SECURITY_LENGTH)
    {
        return 1;
    }

    snprintf(credentials_buf, sizeof(credentials_buf), "%s%s\n%s\n%s\n", FILE_HEADER, ssid, passphrase, security);
    if (mflash_file_save(filename, (uint8_t *)credentials_buf, strlen(credentials_buf) + 1U) != kStatus_Success)
    {
        PRINTF("[!] Failed to save Wi-Fi credentials\r\n");
        return 1;
    }

    PRINTF("[i] Wi-Fi credentials saved to flash\r\n");
    return 0;
}

uint32_t get_saved_wifi_credentials(char *filename, char *ssid, char *passphrase, char *security)
{
    const uint8_t *stored;
    const uint8_t *field_start;
    const uint8_t *field_end;
    uint32_t data_len = 0U;
    status_t status;
    char *fields[3]       = {ssid, passphrase, security};
    size_t limits[3]      = {WPL_WIFI_SSID_LENGTH, WPL_WIFI_PASSWORD_LENGTH, SECURITY_LENGTH};
    const uint8_t *cursor = NULL;
    uint32_t field         = 0U;

    ssid[0] = '\0';
    passphrase[0] = '\0';
    security[0] = '\0';

    status = mflash_file_mmap(filename, &stored, &data_len);
    if (status != kStatus_Success || data_len <= strlen(FILE_HEADER) ||
        strncmp((const char *)stored, FILE_HEADER, strlen(FILE_HEADER)) != 0)
    {
        return 1;
    }

    cursor = stored + strlen(FILE_HEADER);
    for (field = 0U; field < 3U; field++)
    {
        field_start = cursor;
        field_end   = (const uint8_t *)strchr((const char *)field_start, '\n');
        if (field_end == NULL || (size_t)(field_end - field_start) > limits[field])
        {
            return 1;
        }

        memcpy(fields[field], field_start, (size_t)(field_end - field_start));
        fields[field][field_end - field_start] = '\0';
        cursor = field_end + 1U;
    }

    return 0U;
}
