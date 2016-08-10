#ifndef USER_CONFIG_HPP
#define USER_CONFIG_HPP


#define ETH_MAC_LEN 5
#define MAX_APS_TRACKED 20
#define MAX_CLIENTS_TRACKED 0
#define MAX_PROBES_TRACKED 0

#define CHANNEL_HOP_INTERVAL 100


// #define PRINT_ELEMENTS 1

//2hours
#define CLIENT_MAX_TIME 60*60*2

#define SYNC_HOST "t1.lauters.fr"
// #define SYNC_HOST "wifis.lauters.fr"
// #define SYNC_PORT 8686
// #define SYNC_PATH "/esp8266.json"
#define SYNC_MIN_LEVEL -75
// #define SYNC_SSL

// sync data through dns tunneling
#define SYNC_DNS

#ifndef SYNC_DNS
# define SYNC_HTTP
#endif

#include "os_type.h"

// used with sync_type_position will ensure that there will be at least X access point on the fifo
#define MIN_SYNCING_BEST_AP 5

#define SYNC_TYPE sync_type_position

//millis
#define SYNC_PERIOD 1*60*1000

#define MAX_TRIES 5

// uncomment to use over the air upgrade
// #define USE_OTA

// uncomment to toggle at each registered entity (beacon, probe, client...)
// #define LED_TOGGLE_REGISTER

// have a look to wifis_spots.h to configure your wifis access


uint32_t get_seconds();

void toggle_led();
void blink_led(int count, int delay);
void set_led(bool _status);
uint32_t get_button_pressed();

#endif