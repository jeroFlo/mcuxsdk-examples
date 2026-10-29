/*
 * Copyright 2020 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CRED_FLASH_STORAGE_H
#define CRED_FLASH_STORAGE_H

#include <stdint.h>

uint32_t init_flash_storage(char *filename);
uint32_t save_wifi_credentials(char *filename, char *ssid, char *passphrase, char *security);
uint32_t get_saved_wifi_credentials(char *filename, char *ssid, char *passphrase, char *security);

#endif
