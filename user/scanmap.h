#ifndef SCANMAP_H
#define SCANMAP_H

#include "user_config.h"
#include "os_type.h"

struct wifi {
  char essid[33]; 
  char password[33];
};



struct beaconinfo
{
  uint32_t time_s;
  uint8_t bssid[ETH_MAC_LEN];
  uint8_t ssid[33];
  int ssid_len;
  int channel;
  int err;
  signed rssi;
  bool encryption;
  uint8_t capa[2];
};

struct probeinfo
{
  uint32_t time_s;
  uint8_t bssid[ETH_MAC_LEN];
  int ssid_len;
  uint8_t ssid[33];
  int err;
};

struct clientinfo
{
  uint32_t time_s;
  uint8_t bssid[ETH_MAC_LEN];
  uint8_t station[ETH_MAC_LEN];
  uint8_t ap[ETH_MAC_LEN];
  int channel;
  int err;
  signed rssi;
  uint16_t seq_n;
};


union data_item {
  struct beaconinfo beaconinfo;
  struct clientinfo clientinfo;
  struct probeinfo probeinfo;
};

#define T union data_item
#include "fifo.h"

struct data {
  fifo_t beaconsinfos;
  fifo_t probesinfos;
  fifo_t clientsinfos;
  
  union data_item beacons_buffer[MAX_APS_TRACKED + 1];
  union data_item probes_buffer[MAX_PROBES_TRACKED + 1];
  union data_item clients_buffer[MAX_CLIENTS_TRACKED + 1];
  
  int nothing_new;
  bool wififound;
};

#undef T

void ICACHE_FLASH_ATTR scanmap_init();

// enable scanning
void ICACHE_FLASH_ATTR scanmap_enable();

// clear all fifos
void ICACHE_FLASH_ATTR scanmap_reset();

// check if at least on fifo is empty
bool ICACHE_FLASH_ATTR scanmap_isempty();

// check if at least one fifo is full
bool ICACHE_FLASH_ATTR scanmap_isfull();

// print all fifo size
bool ICACHE_FLASH_ATTR scanmap_print_fifos_sizes();

// return a known wifi to connect
struct wifi * ICACHE_FLASH_ATTR scanmap_get_available_wifi();

#endif