#ifndef USER_CONFIG_HPP
#define USER_CONFIG_HPP

#define SSID_NAME ""
#define SSID_PASSWORD ""

#define ETH_MAC_LEN 6
#define MAX_APS_TRACKED 100
#define MAX_CLIENTS_TRACKED 200
#define MAX_PROBES_TRACKED 200

#define CHANNEL_HOP_INTERVAL 100

// #define PRINT_ELEMENTS 1

//2hours
#define CLIENT_MAX_TIME 60*60*2

#define SYNC_HOST "wifis.lauters.fr"
#define SYNC_PORT 6667
#define SYNC_PATH "/esp8266.json"
// #define SYNC_SSL

#include "os_type.h"

#define SYNC_PERIOD 10*1000

#define MAX_TRIES 3

struct data scanmap;
uint32_t get_seconds();

void toggle_led();

#endif