#ifdef USE_OTA

#ifndef UPGRADE_H
#define UPGRADE_H
#include "user_interface.h"

bool ICACHE_FLASH_ATTR handleUpgrade(uint8_t serverVersion, const char *server_ip, uint16_t port, const char *path);

#endif

#endif