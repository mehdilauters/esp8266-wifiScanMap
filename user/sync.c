
#include "sync.h"
#include "scanmap.h"
#include "user_config.h"
#include "user_json.h"
#include <mem.h> 
#include "osapi.h"
#include "upgrade.h"
#include "ip_addr.h"
#include "user_interface.h"
#include "ip_addr.h"
#include "espconn.h"
#include "data.h"

#define WATCHDOG_MS 10000

#ifdef SYNC_SSL
# define CONNECT(conn) espconn_secure_connect( conn )
# define DISCONNECT(conn) espconn_secure_disconnect( conn )
# define SEND(conn, buffer, len) espconn_secure_sent(conn, buffer, len)

unsigned char *default_certificate;
unsigned int default_certificate_len = 0;
unsigned char *default_private_key;
unsigned int default_private_key_len = 0;

#else
# define CONNECT(conn) espconn_connect( conn )
# define DISCONNECT(conn) espconn_disconnect( conn )
# define SEND(conn, buffer, len) espconn_sent(conn, buffer, len)
#endif

struct espconn sync_conn;
ip_addr_t sync_ip;
ip_addr_t my_ip;
esp_tcp sync_tcp;


# define MAX_BUFFER_SIZE 2048

char buffer[ MAX_BUFFER_SIZE ];

#ifdef SYNC_DNS

#define B64_LENGTH(n) (((4 * n / 3) + 3) & ~3)

// b64 size
# define MAX_B64_SIZE 20
#define B64_BUFFER_SIZE B64_LENGTH(MAX_B64_SIZE) + 20

static void send_dns_data(bool retry);
static void dns_sync_done( const char *name, ip_addr_t *ipaddr, void *arg );
char b64_buffer[ B64_BUFFER_SIZE ];
int total_size = 0;
int already_sent = 0;
int dns_tries = 0;
int dns_last_send = 0;
int dns_frame_id =0;

#endif

static volatile os_timer_t sync_timer;
static volatile os_timer_t watchdog_timer;

static void tcp_connect( void *arg );


bool json_put_char(char c) {
  if(strlen(buffer) + 2 >= MAX_BUFFER_SIZE) {
    return false;
  }
  os_sprintf(buffer,"%s%c",buffer, c);
  return true;
}

bool json_put_string(char *s) {
  if(strlen(buffer) + strlen(s) +1 >= MAX_BUFFER_SIZE) {
    os_printf("Size limit: %d",strlen(buffer));
    return false;
  }
  os_sprintf(buffer,"%s%s",buffer, s);
  return true;
}

void connect_station(struct wifi _wifi) {
  
  os_timer_arm(&watchdog_timer, WATCHDOG_MS, 1);
  os_printf("Connecting to %s\n",_wifi.essid);
  
  wifi_set_opmode(STATION_MODE);
  static struct station_config config;
  config.bssid_set = 0;
  os_memcpy( &config.ssid, _wifi.essid, strlen(_wifi.essid) );
  if(strlen(_wifi.password) > 0) {
    os_memcpy( &config.password, _wifi.password, strlen(_wifi.password) );
  } else {
    *config.password = 0;
  }
  wifi_station_set_config_current( &config );
  wifi_station_connect();
}

void sync_sync()
{
  struct wifi * w = scanmap_get_available_wifi(); 
  if(w != NULL) {
    os_timer_disarm(&sync_timer);
    disable_monitor();
    os_printf(">>\n");
    connect_station(*w);
  } else {
    os_printf("--\n"); 
  }
}

void sync_cb(void *arg)
{
  if(SYNC_TYPE == sync_type_time || SYNC_TYPE == sync_type_both) {
    sync_sync();
  }
}

void sync_done(bool ok) {
  os_timer_disarm(&sync_timer);
  os_printf("<<\n");
  
#ifdef USE_OTA
  if(!handleUpgrade(2, "192.168.211.145",8000,"build/app.out")) {
#endif
    wifi_station_disconnect();
    scanmap_enable();
    os_timer_arm(&sync_timer, SYNC_PERIOD, 1);

#ifdef USE_OTA
  }
#endif
  
}

void watchdog_cb(void *arg) {
  os_timer_disarm(&watchdog_timer);
  sync_done(false);
}


#ifdef SYNC_HTTP
void send_data( void *arg ) {
  struct espconn *conn = arg;
  os_memset(buffer, 0, MAX_BUFFER_SIZE);
  os_sprintf( buffer, "POST %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length:      ", SYNC_PATH, SYNC_HOST);
  os_sprintf( buffer, "%s\r\n\r\n",buffer);
  int size = strlen(buffer);
  build_json(&scanmap);

  int new_size = strlen(buffer);
  char tmp[6];
  os_sprintf( tmp, "%5d", new_size - size);
  os_memcpy(&buffer[size-5 - 4], tmp, 5);
  SEND( conn, buffer, os_strlen( buffer ) );
}
#endif


#ifdef SYNC_DNS
void send_dns_data( bool retry ) {
  
  if(! retry )
  {
  
    if(already_sent == total_size || already_sent == 0)
    {
      os_memset(b64_buffer, 0, MAX_B64_SIZE);
      os_memset(buffer, 0, MAX_BUFFER_SIZE);
      build_json(&scanmap);
      total_size = strlen(buffer);
      already_sent = 0;
      dns_frame_id = 0;
    }
    
    // 
    // +1 because of the subdomain '.' and +1 because of \0
    int host_size = strlen(SYNC_HOST) +1+1;
    
    int size = MAX_B64_SIZE - 4;
    if(total_size - already_sent < size) {
      size = total_size - already_sent;
    }
    char tmp[MAX_B64_SIZE];
    os_memset(tmp,0,MAX_B64_SIZE);
    
    uint32_t id = system_get_chip_id();
    os_sprintf(tmp,"%8d",id);
    os_sprintf(tmp+8,"%2d",dns_frame_id);
    
    // add random to avoid cache
    uint16_t rand = os_random();
    os_sprintf(tmp+8 + 2,"%4d",rand);
    os_memcpy( tmp+8 + 2 + 4, buffer+already_sent, size);
    
    int count = base64_encode(size+2+8 + 4, tmp, B64_BUFFER_SIZE, b64_buffer);
    dns_last_send = size;
    already_sent += size;
    char host[255];
    os_sprintf(host,".%s",SYNC_HOST);
    os_strcpy(b64_buffer+count,host);
  }
  err_t res = espconn_gethostbyname( &sync_conn, b64_buffer, &sync_ip, dns_sync_done );
  if(res != ESPCONN_OK && res != ESPCONN_INPROGRESS) {
    os_printf("DNS error %d\n",res);
  }
}
#endif

#ifdef SYNC_HTTP
void data_received( void *arg, char *pdata, unsigned short len )
{
  os_printf( "%s: %s\n", __FUNCTION__, pdata );
}

void check_data(void *arg) {
  if(scanmap_isempty()) {
    struct espconn *conn = arg;
    espconn_delete(conn);
    sync_done(true);
  } else {
    os_printf("sending more data\n");
    tcp_connect( arg );
  }
}

void data_sent(void *arg) {
  struct espconn *conn = arg;
  DISCONNECT( conn );
  check_data(arg);
}

void tcp_connected( void *arg )
{
  struct espconn *conn = arg;
  espconn_regist_recvcb( conn, data_received );
  espconn_regist_sentcb( conn, data_sent);
  
  send_data(arg);
}


void tcp_disconnected( void *arg )
{
  check_data(arg);
}

static void ICACHE_FLASH_ATTR tcp_reconnect(void *arg,
                                                        sint8 errType) {
  os_printf("tcp connect failed\n");
  struct espconn *pespconn = (struct espconn *) arg;
  espconn_delete(pespconn);
  sync_done(false);
}

void tcp_connect( void *arg ) {
  uint8_t count;
  struct espconn *conn = arg;
  
  for(count = 0; count < MAX_TRIES; count++) {
    if(CONNECT(conn)) {
      break;
    }
  }
}
void dns_done( const char *name, ip_addr_t *ipaddr, void *arg )
{
  if ( ipaddr == NULL) 
  {
    os_printf("DNS lookup failed\n");
    sync_done(false);
  }
  else
  {
    os_printf("found server %d.%d.%d.%d\n",
              *((uint8 *)&ipaddr->addr), *((uint8 *)&ipaddr->addr + 1), *((uint8 *)&ipaddr->addr + 2), *((uint8 *)&ipaddr->addr + 3));
    
    struct espconn *conn = arg;
    
    conn->type = ESPCONN_TCP;
    conn->state = ESPCONN_NONE;
    conn->proto.tcp=&sync_tcp;
    conn->proto.tcp->local_port = espconn_port();
    conn->proto.tcp->remote_port = SYNC_PORT;
    os_memcpy( conn->proto.tcp->remote_ip, &ipaddr->addr, 4 );
    
    espconn_regist_connectcb( conn, tcp_connected );
    espconn_regist_disconcb( conn, tcp_disconnected );
    espconn_regist_reconcb(conn, tcp_reconnect);
    
    
    tcp_connect(arg);
  }
}

#endif

#ifdef SYNC_DNS

bool dns_ok(ip_addr_t *ipaddr){
  bool ok = *((uint8 *)&ipaddr->addr) == 0 && *((uint8 *)&ipaddr->addr + 1) == 0 && *((uint8 *)&ipaddr->addr + 2) == 0  && *((uint8 *)&ipaddr->addr + 3) == 0;
  if(!ok) {
    os_printf("protocol error %d.%d.%d.%d\n",
        *((uint8 *)&ipaddr->addr), *((uint8 *)&ipaddr->addr + 1), *((uint8 *)&ipaddr->addr + 2), *((uint8 *)&ipaddr->addr + 3));
  }
  return ok;
}

void dns_sync_done( const char *name, ip_addr_t *ipaddr, void *arg ) {
  if ( ipaddr == NULL) 
  {
    os_printf("DNS lookup failed ");
    dns_tries++;
    if(dns_tries >= MAX_TRIES) {
      os_printf("aborting\n");
      dns_tries = 0;
      total_size = 0;
      already_sent = 0;
      dns_frame_id = 0;
      sync_done(false);
      return;
    }
    already_sent -= dns_last_send;
    os_printf("retrying\n");
    send_dns_data(true);
  }
  else
  {
    if(dns_ok(ipaddr))
    {
      dns_frame_id++;
      dns_tries = 0;
//       os_printf("DNS SYNC DONE %d %d\n",already_sent, total_size);
      if(scanmap_isempty() && already_sent >= total_size) {
        sync_done(true);
      } else {
        send_dns_data(false);
      } 
    }
    else
    {
      dns_tries++;
      if(dns_tries >= MAX_TRIES) {
        os_printf("aborting\n");
        dns_tries = 0;
        total_size = 0;
        already_sent = 0;
        dns_frame_id = 0;
        sync_done(false);
        return;
      }
      already_sent -= dns_last_send;
      os_printf("retrying\n");
      send_dns_data(true);
    }
  }
}
#endif

void wifi_callback( System_Event_t *evt )
{
//   os_printf( "%s: %d\n", __FUNCTION__, evt->event );
  
  switch ( evt->event )
  {
    case EVENT_STAMODE_CONNECTED:
    {
//       os_printf("connect to ssid %s, channel %d\n",
//                 evt->event_info.connected.ssid,
//                 evt->event_info.connected.channel);
      break;
    }
    
    case EVENT_STAMODE_DISCONNECTED:
    {
//       os_printf("disconnect from ssid %s, reason %d\n",
//                 evt->event_info.disconnected.ssid,
//                 evt->event_info.disconnected.reason);
      break;
    }
    
    case EVENT_STAMODE_GOT_IP:
    {
      os_timer_disarm(&watchdog_timer);
//       os_printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
//                 IP2STR(&evt->event_info.got_ip.ip),
//                 IP2STR(&evt->event_info.got_ip.mask),
//                 IP2STR(&evt->event_info.got_ip.gw));
//       os_printf("\n");
      #ifdef SYNC_HTTP
        os_printf("\nHTTP sync\n");
        espconn_gethostbyname( &sync_conn, SYNC_HOST, &sync_ip, dns_done );
      #elif defined(SYNC_DNS)
        os_printf("\nDNS sync\n");
        send_dns_data(false);
      #else
        os_printf("ERROR: no transport protocol\n");
      #endif
      break;
    }
    
    default:
    {
      break;
    }
  }
}

void sync_init() {
  wifi_set_event_handler_cb( wifi_callback );
  
  #ifdef SYNC_SSL
//     espconn_secure_ca_disable(0x1);
  #endif
  
  os_timer_disarm(&sync_timer);
  os_timer_setfn(&sync_timer, (os_timer_func_t *) sync_cb, NULL);
  os_timer_arm(&sync_timer, SYNC_PERIOD, 1);
  
  os_timer_disarm(&watchdog_timer);
  os_timer_setfn(&watchdog_timer, (os_timer_func_t *) watchdog_cb, NULL);
}