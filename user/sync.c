
#include "sync.h"
#include "scanmap.h"
#include "user_config.h"
#include "user_json.h"
#include <mem.h> 
#include "osapi.h"

#include "ip_addr.h"
#include "user_interface.h"
#include "ip_addr.h"
#include "espconn.h"


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

#define MAX_BUFFER_SIZE 2048
char buffer[ MAX_BUFFER_SIZE ];

static volatile os_timer_t sync_timer;

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

void connect_station() {
  os_printf("Connecting to %s\n",SSID_NAME);
  wifi_set_opmode(STATION_MODE);
  
  static struct station_config config;
  config.bssid_set = 0;
  os_memcpy( &config.ssid, SSID_NAME, 32 );
  os_memcpy( &config.password, SSID_PASSWORD, 64 );
  wifi_station_set_config_current( &config );
  wifi_station_connect();
}

void sync_cb(void *arg)
{
  os_timer_disarm(&sync_timer);
  disable_monitor();
  os_printf("== Synchro start ==\n");
  scanmap_print_fifos_sizes();
  connect_station() ;
}

void sync_done(bool ok) {
  os_timer_disarm(&sync_timer);
  wifi_station_disconnect();
  os_printf("== Synchro end ==\n");
  scanmap_print_fifos_sizes();
  scanmap_enable();
  os_timer_arm(&sync_timer, SYNC_PERIOD, 1);
}


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

//   os_printf( "Sending: %s\n", buffer );
  SEND( conn, buffer, os_strlen( buffer ) );
}

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
//       os_printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
//                 IP2STR(&evt->event_info.got_ip.ip),
//                 IP2STR(&evt->event_info.got_ip.mask),
//                 IP2STR(&evt->event_info.got_ip.gw));
//       os_printf("\n");
      
      espconn_gethostbyname( &sync_conn, SYNC_HOST, &sync_ip, dns_done );
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
}