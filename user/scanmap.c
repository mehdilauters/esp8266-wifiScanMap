#include "scanmap.h"
#include "user_config.h"
#include <mem.h> 
#include "osapi.h"

#include "ip_addr.h"
#include "user_interface.h"
#include "sync.h"
#include "wifis_spots.h"
#include "data.h"

#ifdef LED_TOGGLE_REGISTER
#define LED_TOGGLE() toggle_led();
#else
#define LED_TOGGLE()
#endif

static volatile os_timer_t channelHop_timer;

void channelHop(void *arg)
{
  // 1 - 13 channel hopping
  uint8 new_channel = wifi_get_channel() % 12 + 1;
  wifi_set_channel(new_channel);
}

uint8_t broadcast1[3] = { 0x01, 0x00, 0x5e };
uint8_t broadcast2[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
uint8_t broadcast3[3] = { 0x33, 0x33, 0x00 };


void ICACHE_FLASH_ATTR scanmap_clear() {
  fifo_clear(&scanmap.beaconsinfos);
  fifo_clear(&scanmap.probesinfos);
  fifo_clear(&scanmap.clientsinfos);
}

void ICACHE_FLASH_ATTR print_beacon(struct beaconinfo beacon)
{
  if (beacon.err != 0) {
    //os_printf("BEACON ERR: (%d)  ", beacon.err);
  } else {
    os_printf("BEACON: [%32s]  ", beacon.ssid);
    int i = 0;
    for (i = 0; i < 6; i++) os_printf("%02x", beacon.bssid[i]);
    os_printf("   %2d", beacon.channel);
    if(! beacon.encryption ) {
      os_printf("   OPN");
    } else {
      os_printf("   KEY");
    }
    os_printf("   %4d\r\n", beacon.rssi);
  }
}

void ICACHE_FLASH_ATTR print_probe(struct probeinfo pi)
{
  if (pi.err != 0) {
    //os_printf("PROBE ERR: (%d)  ", beacon.err);
  } else {
    os_printf("PROBE: [%32s]  ", pi.ssid);
    int i = 0;
    for (i = 0; i < 6; i++) os_printf("%02x", pi.bssid[i]);
    os_printf("\r\n");
  }
}

void ICACHE_FLASH_ATTR print_client(struct clientinfo ci)
{
  uint16_t u = 0;
  int i = 0;
  int known = 0;   // Clear known flag
  if (ci.err != 0) {
  } else {
    os_printf("CLIENT: ");
    int i = 0;
    for (i = 0; i < 6; i++) os_printf("%02x", ci.station[i]);
    os_printf(" works with: ");
    struct beaconinfo item = fifo_at(&scanmap.beaconsinfos, u).beaconinfo;
    for (u = 0; u < fifo_size(&scanmap.clientsinfos); u++)
    {
      if (! memcmp(item.bssid, ci.bssid, ETH_MAC_LEN)) {
        os_printf("[%32s]", item.ssid);
        known = 1;
        break;
      }   // AP known => Set known flag
    }
    if (! known)  {
      os_printf("%22s", " ");
      for (i = 0; i < 6; i++) os_printf("%02x", ci.bssid[i]);
    }
    
    os_printf("%5s", " ");
    for (i = 0; i < 6; i++) os_printf("%02x", ci.ap[i]);
    os_printf("%5s", " ");
    
    if (! known) {
      os_printf("   %3d", ci.channel);
    } else {
      os_printf("   %3d", item.channel);
    }
    os_printf("   %4d\r\n", ci.rssi);
  }
}

bool is_valid_string(char * _s, int _len) {
  int i;
  for(i = 0; i<_len; i++) {
    if(_s[i] < 32 || _s[i] > 126) {
      return false;
    }
  }
  return true;
}

struct probeinfo ICACHE_FLASH_ATTR parse_probe(uint8_t *frame, uint16_t framelen)
{
  struct probeinfo pi;
  pi.ssid_len = 0;
  pi.err = 0;
  int pos = 24;
  if (frame[pos] == 0x00) {
    while (pos < framelen) {
      switch (frame[pos]) {
        case 0x00: //SSID
          pi.ssid_len = (int) frame[pos + 1];
          if (pi.ssid_len == 0) {
            memset(pi.ssid, '\x00', 33);
            break;
          }
          if (pi.ssid_len < 2) {
            pi.err = -1;
            break;
          }
          if (pi.ssid_len > 32) {
            pi.err = -2;
            break;
          }
          memset(pi.ssid, '\x00', 33);
          memcpy(pi.ssid, frame + pos + 2, pi.ssid_len);
          
          pi.err = !is_valid_string(pi.ssid, pi.ssid_len);
          
          break;
      }
      pos++;
    }
  }
  memcpy(pi.bssid, frame + 10, ETH_MAC_LEN);
  pi.time_s = get_seconds();
  return pi;
}

struct beaconinfo ICACHE_FLASH_ATTR parse_beacon(uint8_t *frame, uint16_t framelen, signed rssi)
{
  struct beaconinfo bi;
  bi.ssid_len = 0;
  bi.channel = 0;
  bi.err = 0;
  bi.rssi = rssi;
  bi.encryption = false;
  int pos = 36;
  bool ssid_found = false;
  if (frame[pos] == 0x00) {    
    while (pos < framelen) {
      switch (frame[pos]) {
        case 0x00: //SSID
          if(ssid_found) break;
          ssid_found = true;
          bi.ssid_len = (int) frame[pos + 1];
          if (bi.ssid_len == 0) {
            memset(bi.ssid, '\x00', 33);
            break;
          }
          if (bi.ssid_len < 0) {
            bi.err = -1;
            break;
          }
          if (bi.ssid_len > 32) {
            bi.err = -2;
            break;
          }
          
          
          memset(bi.ssid, '\x00', 33);
          memcpy(bi.ssid, frame + pos + 2, bi.ssid_len);
          
          bi.err = !is_valid_string(bi.ssid, bi.ssid_len);
          
          if(!bi.err && !scanmap.wififound) {
            int i;
            for(i=0; i < sizeof(wifis_spots) / sizeof(struct wifi); i++) {
              if (! memcmp(wifis_spots[i].essid, bi.ssid, bi.ssid_len)) {
                if(bi.rssi > SYNC_MIN_LEVEL) {
                  scanmap.wififound = true;
                  blink_led(5, 500);
                }
              }
            }
          }
          
          break;
        case 0x03: //Channel
          bi.channel = (int) frame[pos + 2];
          //pos = -1;
          break;
        case 48: //RSN
          bi.encryption = true;
          //pos = -1;
          break;
        default:
          break;
      }
      if (pos < 0) break;
      pos += (int) frame[pos + 1] + 2;
    }
  } else {
    bi.err = -3;
  }
  
  bi.capa[0] = frame[34];
  bi.capa[1] = frame[35];
  memcpy(bi.bssid, frame + 10, ETH_MAC_LEN);
  
  bi.time_s = get_seconds();
  
  return bi;
}

struct clientinfo ICACHE_FLASH_ATTR parse_data(uint8_t *frame, uint16_t framelen, signed rssi, unsigned channel)
{
  struct clientinfo ci;
  ci.channel = channel;
  ci.err = 0;
  ci.rssi = rssi;
  int pos = 36;
  uint8_t *bssid;
  uint8_t *station;
  uint8_t *ap;
  uint8_t ds;
  
  ds = frame[1] & 3;    //Set first 6 bits to 0
  switch (ds) {
    // p[1] - xxxx xx00 => NoDS   p[4]-DST p[10]-SRC p[16]-BSS
    case 0:
      bssid = frame + 16;
      station = frame + 10;
      ap = frame + 4;
      break;
      // p[1] - xxxx xx01 => ToDS   p[4]-BSS p[10]-SRC p[16]-DST
    case 1:
      bssid = frame + 4;
      station = frame + 10;
      ap = frame + 16;
      break;
      // p[1] - xxxx xx10 => FromDS p[4]-DST p[10]-BSS p[16]-SRC
    case 2:
      bssid = frame + 10;
      // hack - don't know why it works like this...
      if (memcmp(frame + 4, broadcast1, 3) || memcmp(frame + 4, broadcast2, 3) || memcmp(frame + 4, broadcast3, 3)) {
        station = frame + 16;
        ap = frame + 4;
      } else {
        station = frame + 4;
        ap = frame + 16;
      }
      break;
      // p[1] - xxxx xx11 => WDS    p[4]-RCV p[10]-TRM p[16]-DST p[26]-SRC
    case 3:
      bssid = frame + 10;
      station = frame + 4;
      ap = frame + 4;
      break;
  }
  
  memcpy(ci.station, station, ETH_MAC_LEN);
  memcpy(ci.bssid, bssid, ETH_MAC_LEN);
  memcpy(ci.ap, ap, ETH_MAC_LEN);
  
  ci.seq_n = frame[23] * 0xFF + (frame[22] & 0xF0);
  
  ci.time_s = get_seconds();
  return ci;
}


int ICACHE_FLASH_ATTR register_beacon(struct beaconinfo beacon)
{
  int known = 0;   // Clear known flag
  uint16_t u = 0;
  for (u = 0; u < fifo_size(&scanmap.beaconsinfos); u++)
  {
    struct beaconinfo item = fifo_at(&scanmap.beaconsinfos, u).beaconinfo;
    if (! memcmp(item.bssid, beacon.bssid, ETH_MAC_LEN)) {
      known = 1;
      break;
    }   // AP known => Set known flag
  }
  if (! known)  // AP is NEW, copy MAC to array and return it
  {
    
    bool synced = false;
    if(fifo_isfull(&scanmap.beaconsinfos)) {
      fifo_pop(&scanmap.beaconsinfos);
      os_printf("exceeded max scanmap.aps_known\n");
      if(SYNC_TYPE == sync_type_full || SYNC_TYPE == sync_type_both) {
        sync_sync();
        synced = true;
      }
    }
    if(!synced && SYNC_TYPE == sync_type_position) {
      if(fifo_size(&scanmap.beaconsinfos) >= MIN_SYNCING_BEST_AP) {
        sync_sync();
      }
    }
    if(!fifo_isfull(&scanmap.beaconsinfos)) {
      union data_item item;
      item.beaconinfo = beacon;
      fifo_push(&scanmap.beaconsinfos, item);      
      LED_TOGGLE();
    }
  }
  return known;
}

int ICACHE_FLASH_ATTR register_probe(struct probeinfo pi)
{
  if(pi.err) {
    return 0;
  }
  int known = 0;   // Clear known flag
  uint16_t u = 0;
  for (u = 0; u < fifo_size(&scanmap.probesinfos); u++)
  {
    struct probeinfo item = fifo_at(&scanmap.probesinfos,u).probeinfo;
    bool mac_equals = memcmp(item.bssid, pi.bssid, ETH_MAC_LEN) == 0;
    bool ssid_equals = ( item.ssid_len ==  pi.ssid_len && memcmp(item.ssid, pi.ssid, item.ssid_len) ==0);
    //     os_printf("%d - %d\n",mac_equals, ssid_equals);
    if ( mac_equals && ssid_equals) {
      known = 1;
      break;
    }
  }
  if (! known)
  { 
    if(fifo_isfull(&scanmap.probesinfos) && MAX_PROBES_TRACKED != 0) {
      fifo_pop(&scanmap.probesinfos);
      os_printf("exceeded max scanmap.probes_known\n"); 
      if(SYNC_TYPE == sync_type_full || SYNC_TYPE == sync_type_both) {
        sync_sync();
      }
    }
    if(!fifo_isfull(&scanmap.probesinfos)) {
      union data_item item;
      item.probeinfo = pi;
      fifo_push(&scanmap.probesinfos, item); 
      LED_TOGGLE();
    }
  }
  return known;
}

int ICACHE_FLASH_ATTR register_client(struct clientinfo ci)
{
  int known = 0;   // Clear known flag
  uint16_t u = 0;
  for (u = 0; u < fifo_size(&scanmap.clientsinfos); u++)
  {
    struct clientinfo item = fifo_at(&scanmap.clientsinfos,u).clientinfo;
    if (! memcmp(item.station, ci.station, ETH_MAC_LEN) && get_seconds() - ci.time_s < CLIENT_MAX_TIME) {
      known = 1;
      break;
    }
  }
  if (! known)
  {
    if(fifo_isfull(&scanmap.clientsinfos) && MAX_CLIENTS_TRACKED != 0) {
      fifo_pop(&scanmap.clientsinfos);
      os_printf("exceeded max scanmap.clients_known\n");
      if(SYNC_TYPE == sync_type_full || SYNC_TYPE == sync_type_both) {
        sync_sync();
      }
    }
    if(! fifo_isfull(&scanmap.clientsinfos)) {
      union data_item item;
      item.clientinfo = ci;
      fifo_push(&scanmap.clientsinfos, item);
      LED_TOGGLE();
    }
  }
  return known;
}

/* ==============================================
 * Promiscous callback structures, see ESP manual
 * ============================================== */

struct RxControl {
  signed rssi:8;
  // signal intensity of packet
  unsigned rate:4;
  unsigned is_group:1;
  unsigned:1;
  unsigned sig_mode:2;
  // 0:is 11n packet; 1:is not 11n packet;
  unsigned legacy_length:12; // if not 11n packet, shows length of packet.
  unsigned damatch0:1;
  unsigned damatch1:1;
  unsigned bssidmatch0:1;
  unsigned bssidmatch1:1;
  unsigned MCS:7;
  // if is 11n packet, shows the modulation
  // and code used (range from 0 to 76)
  unsigned CWB:1; // if is 11n packet, shows if is HT40 packet or not
  unsigned HT_length:16;// if is 11n packet, shows length of packet.
  unsigned Smoothing:1;
  unsigned Not_Sounding:1;
  unsigned:1;
  unsigned Aggregation:1;
  unsigned STBC:2;
  unsigned FEC_CODING:1; // if is 11n packet, shows if is LDPC packet or not.
  unsigned SGI:1;
  unsigned rxend_state:8;
  unsigned ampdu_cnt:8;
  unsigned channel:4; //which channel this packet in.
  unsigned:12;
};
struct LenSeq{
  u16 len; // length of packet
  u16 seq; // serial number of packet, the high 12bits are serial number,
  //  low 14 bits are Fragment number (usually be 0)
  u8 addr3[6]; // the third address in packet
};
struct sniffer_buf{
  struct RxControl rx_ctrl;
  u8 buf[36 ]; // head of ieee80211 packet
  u16 cnt;
  // number count of packet
  struct LenSeq lenseq[1];
  //length of packet
};
struct sniffer_buf2{
  struct RxControl rx_ctrl;
  u8 buf[112];
  u16 cnt;
  u16 len;
  //length of packet
};



void ICACHE_FLASH_ATTR promisc_cb(uint8_t *buf, uint16_t len)
{
  int i = 0;
  uint16_t seq_n_new = 0;
  if (len == 12) {
    struct RxControl *sniffer = (struct RxControl*) buf;
  } else if (len == 128) {
    struct sniffer_buf2 *sniffer = (struct sniffer_buf2*) buf;
    
    if(sniffer->buf[0] == 0x80) {
      struct beaconinfo beacon = parse_beacon(sniffer->buf, 112, sniffer->rx_ctrl.rssi);
      
      if (register_beacon(beacon) == 0) {
        #ifdef PRINT_ELEMENTS
          print_beacon(beacon);
        #endif
        scanmap.nothing_new = 0;
      }
    } else if(sniffer->buf[0] == 0x40) {
      struct probeinfo pi = parse_probe(sniffer->buf, 34);
      if(strlen(pi.ssid) != 0) {
        if(register_probe(pi) == 0) {
          #ifdef PRINT_ELEMENTS
            print_probe(pi);
          #endif
          scanmap.nothing_new = 0;
        }
      }
    }
  } else {
    //Serial.println(len);
    struct sniffer_buf *sniffer = (struct sniffer_buf*) buf;
    //Is data or QOS?
    if ((sniffer->buf[0] == 0x08) || (sniffer->buf[0] == 0x88)) {
      struct clientinfo ci = parse_data(sniffer->buf, 36, sniffer->rx_ctrl.rssi, sniffer->rx_ctrl.channel);
      if (memcmp(ci.bssid, ci.station, ETH_MAC_LEN)) {
        if (register_client(ci) == 0) {
          #ifdef PRINT_ELEMENTS
            print_client(ci);
          #endif
          scanmap.nothing_new = 0;
        }
      }
    }
  }
}




void ICACHE_FLASH_ATTR enable_monitor() {
  os_printf("enable monitor\n");
  os_timer_arm(&channelHop_timer, CHANNEL_HOP_INTERVAL, 1);
  
  // Promiscuous works only with station mode
  wifi_station_disconnect();
  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(promisc_cb);
  wifi_promiscuous_enable(1);
}

void ICACHE_FLASH_ATTR disable_monitor() {
  os_printf("disable monitor\n");
  os_timer_disarm(&channelHop_timer);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(0);
}

void ICACHE_FLASH_ATTR scanmap_enable() {
  enable_monitor();
}


void ICACHE_FLASH_ATTR scanmap_reset() {
  scanmap_clear();
  enable_monitor();
}

bool ICACHE_FLASH_ATTR scanmap_print_fifos_sizes() {
  os_printf("beacons %d\n",fifo_size(&scanmap.beaconsinfos));
  os_printf("probes %d\n",fifo_size(&scanmap.probesinfos));
  os_printf("clients %d\n",fifo_size(&scanmap.clientsinfos));
}

bool ICACHE_FLASH_ATTR scanmap_isempty() {
  return fifo_isempty(&scanmap.beaconsinfos) && fifo_isempty(&scanmap.probesinfos) && fifo_isempty(&scanmap.clientsinfos);
}

bool ICACHE_FLASH_ATTR scanmap_isfull() {
  return fifo_isfull(&scanmap.beaconsinfos) || fifo_isfull(&scanmap.probesinfos) || fifo_isfull(&scanmap.clientsinfos);
}

struct wifi * ICACHE_FLASH_ATTR scanmap_get_available_wifi() {
  uint8_t i,u;
  
  for(i=0; i < sizeof(wifis_spots) / sizeof(struct wifi); i++) {
    
    for (u = 0; u < fifo_size(&scanmap.beaconsinfos); u++)
    {
      struct beaconinfo item = fifo_at(&scanmap.beaconsinfos, u).beaconinfo;
//       check for any open network
      if(strlen(wifis_spots[i].essid) == 0 && ! item.encryption && item.rssi >= SYNC_MIN_LEVEL) {
        return &wifis_spots[i];
      } else if(strlen(wifis_spots[i].essid) == item.ssid_len)
        if (! memcmp(wifis_spots[i].essid, item.ssid, item.ssid_len) && item.rssi >= SYNC_MIN_LEVEL) {
          return &wifis_spots[i];
        }
    }
  }
  return NULL;
}

void ICACHE_FLASH_ATTR scanmap_init() {
  
  fifo_init(&scanmap.beaconsinfos, scanmap.beacons_buffer, MAX_APS_TRACKED+1);
  fifo_init(&scanmap.probesinfos, scanmap.probes_buffer, MAX_PROBES_TRACKED+1);
  fifo_init(&scanmap.clientsinfos, scanmap.clients_buffer, MAX_CLIENTS_TRACKED+1);
  
  scanmap.wififound = false;
  
  os_timer_disarm(&channelHop_timer);
  os_timer_setfn(&channelHop_timer, (os_timer_func_t *) channelHop, NULL);
  
  scanmap_reset();
}