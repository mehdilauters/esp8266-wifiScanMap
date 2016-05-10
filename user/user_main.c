#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
// #include "uart.h"
// #include "driver/uart_register.h"
#include "os_type.h"
#include "user_config.h"
#include "user_interface.h"

#include "espconn.h"
#include "ip_addr.h"

#define ETH_MAC_LEN 6
#define MAX_APS_TRACKED 100
#define MAX_CLIENTS_TRACKED 200
#define MAX_PROBES_TRACKED 200

#define CHANNEL_HOP_INTERVAL 100

#define PRINT_ELEMENTS 1

uint8_t channel = 0;

#define user_procTaskPrio        0
#define user_procTaskQueueLen    1
os_event_t    user_procTaskQueue[user_procTaskQueueLen];
static void user_procTask(os_event_t *events);

static volatile os_timer_t channelHop_timer;
static volatile os_timer_t station_timer;

void user_rf_pre_init(void) {
}


void channelHop(void *arg)
{
  // 1 - 13 channel hopping
  uint8 new_channel = wifi_get_channel() % 12 + 1;
//   os_printf("switching channel %d\n", new_channel);
  wifi_set_channel(new_channel);
}




void hex_print(char *p, size_t n)
{
  char HEX[]="0123456789ABCDEF";
  unsigned int i,j,count;
  j=0;
  i=0;
  count=0;
  while(j < n)
  {
    count++;
    os_printf("0x%02x\t",count);
    if(j+16<n){
      for(i=0;i<16;i++)
      {
        os_printf("0x%c%c ",HEX[(p[j+i]&0xF0) >> 4],HEX[p[j+i]&0xF]);
      }
      os_printf("\t");
      for(i=0;i<16;i++)
      {
        os_printf("%c",isprint(p[j+i])?p[j+i]:'.');
      }
      os_printf("\n");
      j = j+16;
    }
    else
    {
      for(i=0;i<n-j;i++)
      {
        os_printf("0x%c%c ",HEX[(p[j+i]&0xF0) >> 4],HEX[p[j+i]&0xF]);
      }
      os_printf("\t");
      for(i=0;i<n-j;i++)
      {
        os_printf("%c",isprint(p[j+i])?p[j+i]:'.');
      }
      os_printf("\n");
      break;
    }
  }
}


uint8_t broadcast1[3] = { 0x01, 0x00, 0x5e };
uint8_t broadcast2[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
uint8_t broadcast3[3] = { 0x33, 0x33, 0x00 };


struct beaconinfo
{
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
  uint8_t bssid[ETH_MAC_LEN];
  int ssid_len;
  uint8_t ssid[33];
  int err;
};

struct clientinfo
{
  uint8_t bssid[ETH_MAC_LEN];
  uint8_t station[ETH_MAC_LEN];
  uint8_t ap[ETH_MAC_LEN];
  int channel;
  int err;
  signed rssi;
  uint16_t seq_n;
};

struct beaconinfo aps_known[MAX_APS_TRACKED];                    // Array to save MACs of known APs
int aps_known_count = 0;                                  // Number of known APs
int nothing_new = 0;
struct clientinfo clients_known[MAX_CLIENTS_TRACKED];            // Array to save MACs of known CLIENTs
int clients_known_count = 0;                              // Number of known CLIENTs
struct probeinfo probes_known[MAX_PROBES_TRACKED];
int probes_known_count = 0;

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
          pi.err = 0;  // before was error??
          break;
      }
      pos++;
    }
  }
  memcpy(pi.bssid, frame + 10, ETH_MAC_LEN);
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

          bi.err = 0;  // before was error??
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
  
  return ci;
}

int ICACHE_FLASH_ATTR register_beacon(struct beaconinfo beacon)
{
  int known = 0;   // Clear known flag
  int u = 0;
  for (u = 0; u < aps_known_count; u++)
  {
    if (! memcmp(aps_known[u].bssid, beacon.bssid, ETH_MAC_LEN)) {
      known = 1;
      break;
    }   // AP known => Set known flag
  }
  if (! known)  // AP is NEW, copy MAC to array and return it
  {
    memcpy(&aps_known[aps_known_count], &beacon, sizeof(beacon));
    aps_known_count++;
    
    if ((unsigned int) aps_known_count >=
      sizeof (aps_known) / sizeof (aps_known[0]) ) {
      os_printf("exceeded max aps_known\n");
    aps_known_count = 0;
      }
  }
  return known;
}

int ICACHE_FLASH_ATTR register_probe(struct probeinfo pi)
{
  int known = 0;   // Clear known flag
  int u = 0;
  for (u = 0; u < probes_known_count; u++)
  {
    bool mac_equals = memcmp(probes_known[u].bssid, pi.bssid, ETH_MAC_LEN) == 0;
    bool ssid_equals = ( probes_known[u].ssid_len ==  pi.ssid_len && memcmp(probes_known[u].ssid, pi.ssid, probes_known[u].ssid_len) ==0);
//     os_printf("%d - %d\n",mac_equals, ssid_equals);
    if ( mac_equals && ssid_equals) {
        known = 1;
        break;
      }
  }
  if (! known)
  {
    memcpy(&probes_known[probes_known_count], &pi, sizeof(pi));
    probes_known_count++;
    
    if ((unsigned int) probes_known_count >=
      sizeof (probes_known) / sizeof (probes_known[0]) ) {
      os_printf("exceeded max probes_known\n");
    probes_known_count = 0;
      }
  }
  return known;
}

int ICACHE_FLASH_ATTR register_client(struct clientinfo ci)
{
  int known = 0;   // Clear known flag
  int u = 0;
  for (u = 0; u < clients_known_count; u++)
  {
    if (! memcmp(clients_known[u].station, ci.station, ETH_MAC_LEN)) {
      known = 1;
      break;
    }
  }
  if (! known)
  {
    memcpy(&clients_known[clients_known_count], &ci, sizeof(ci));
    clients_known_count++;
    
    if ((unsigned int) clients_known_count >=
      sizeof (clients_known) / sizeof (clients_known[0]) ) {
      os_printf("exceeded max clients_known\n");
    clients_known_count = 0;
      }
  }
  return known;
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
  int u = 0;
  int i = 0;
  int known = 0;   // Clear known flag
  if (ci.err != 0) {
  } else {
    os_printf("CLIENT: ");
    int i = 0;
    for (i = 0; i < 6; i++) os_printf("%02x", ci.station[i]);
    os_printf(" works with: ");
    for (u = 0; u < aps_known_count; u++)
    {
      if (! memcmp(aps_known[u].bssid, ci.bssid, ETH_MAC_LEN)) {
        os_printf("[%32s]", aps_known[u].ssid);
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
      os_printf("   %3d", aps_known[u].channel);
    }
    os_printf("   %4d\r\n", ci.rssi);
  }
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
//       os_printf("======\n");
//       hex_print(buf, 112);
      struct beaconinfo beacon = parse_beacon(sniffer->buf, 112, sniffer->rx_ctrl.rssi);
  
      if (register_beacon(beacon) == 0) {
        #ifdef PRINT_ELEMENTS
        print_beacon(beacon);
        //Serial.println("+");
        #endif
        nothing_new = 0;
      }
    } else if(sniffer->buf[0] == 0x40) {
      struct probeinfo pi = parse_probe(sniffer->buf, 34);
      if(register_probe(pi) == 0) {
        #ifdef PRINT_ELEMENTS
        //Serial.println("*");
        print_probe(pi);
        #endif
        nothing_new = 0;
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
          //Serial.println("-");
          #endif
          nothing_new = 0;
        }
      }
    }
  }
}




void ICACHE_FLASH_ATTR enable_monitor() {
  os_timer_arm(&channelHop_timer, CHANNEL_HOP_INTERVAL, 1);
  // Promiscuous works only with station mode
  wifi_station_disconnect();
  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(promisc_cb);
  wifi_promiscuous_enable(1);
}

void ICACHE_FLASH_ATTR disable_monitor() {
  os_timer_disarm(&channelHop_timer);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(0);
}


//Do nothing function
static void ICACHE_FLASH_ATTR
user_procTask(os_event_t *events)
{
    os_delay_us(10);
}

void connect_station() {
//   disable_monitor();

  wifi_set_opmode(STATION_MODE);
  
  static struct station_config config;
  config.bssid_set = 0;
  os_memcpy( &config.ssid, SSID_NAME, 32 );
  os_memcpy( &config.password, SSID_PASSWORD, 64 );
  wifi_station_set_config_current( &config );
  wifi_station_connect();
}

void ICACHE_FLASH_ATTR init_done(void) {
//   os_printf("\n\nSDK version:%s\n", system_get_sdk_version());
  os_printf("Sdk version %s\n", system_get_sdk_version());
  enable_monitor();
  os_printf("Monitor\n\r");
}


void station_cb(void *arg)
{
  os_printf("Station setup\n");
  os_timer_disarm(&station_timer);
  disable_monitor();
  connect_station() ;
}

//Init function 
void ICACHE_FLASH_ATTR
user_init()
{
  
  // avoid error: pll_cal exceeds 2ms!!!
  wifi_set_sleep_type(NONE_SLEEP_T);
  
  
  uart_div_modify(0, UART_CLK_FREQ / 115200);
  
  // Initialize the GPIO subsystem.
  gpio_init();

  //Set GPIO2 to output mode
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);

  //Set GPIO2 low
  gpio_output_set(0, BIT2, BIT2, 0);

  system_init_done_cb(init_done);
  
  
  

  
  os_timer_disarm(&channelHop_timer);
  os_timer_setfn(&channelHop_timer, (os_timer_func_t *) channelHop, NULL);
  
  os_timer_disarm(&station_timer);
//   os_timer_setfn(&station_timer, (os_timer_func_t *) station_cb, NULL);
//   os_timer_arm(&station_timer, 1000*10, 1);
    
  //Start os task
  system_os_task(user_procTask, user_procTaskPrio,user_procTaskQueue, user_procTaskQueueLen);
  
  
  os_printf("Hello\n\r");
}
