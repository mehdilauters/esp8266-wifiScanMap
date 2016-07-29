#ifdef USE_OTA

#include "upg.h"

#include "upgrade.h"
#include "user_interface.h"
#include "ip_addr.h"
#include "ets_sys.h"
#include "osapi.h"
#include "espconn.h"
#include <mem.h> 


static void ICACHE_FLASH_ATTR ota_finished_callback(void *arg)
{
  struct upgrade_server_info *update = (struct upgrade_server_info *)arg;
  if (update->upgrade_flag == true)
  {
    os_printf("[OTA]success; rebooting!\n");
    system_upgrade_reboot();
  }
  else
  {
    os_printf("[OTA]failed!\n");
  }
  
  os_free(update->pespconn);
  os_free(update->url);
  os_free(update);
}

bool ICACHE_FLASH_ATTR handleUpgrade(uint8_t serverVersion, const char *server_ip, uint16_t port, const char *path)
{
  const char* file;
  uint8_t userBin = system_upgrade_userbin_check();
  switch (userBin)
  {
    case UPGRADE_FW_BIN1: file = "user2.bin"; break;
    case UPGRADE_FW_BIN2: file = "user1.bin"; break;
    default:
      os_printf("[OTA]Invalid userbin number!\n");
      return false;
  }
  
  uint16_t version=1;
  if (serverVersion <= version)
  {
    os_printf("[OTA]No update. Server version:%d, local version %d\n", serverVersion, version);
    return false;
  }
  
  os_printf("[OTA]Upgrade available version: %d\n", serverVersion);
  
  struct upgrade_server_info* update = (struct upgrade_server_info *)os_zalloc(sizeof(struct upgrade_server_info));
  update->pespconn = (struct espconn *)os_zalloc(sizeof(struct espconn));
  
  update->ip[0] = 192;
  update->ip[1] = 168;
  update->ip[2] = 211;
  update->ip[3] = 145;
//   os_memcpy(update->ip, server_ip, 4);
  update->port = port;
  
  os_printf("[OTA]Server "IPSTR":%d. Path: %s\n", IP2STR(update->ip), update->port, path);
  
  update->check_cb = ota_finished_callback;
  update->check_times = 120000;
  update->url = (uint8 *)os_zalloc(512);
  
  os_sprintf((char*)update->url,
             "GET %s HTTP/1.1\r\n"
             "Host: "IPSTR":%d\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, IP2STR(update->ip), update->port);
  os_printf(update->url);
  system_upgrade_flag_set(0x00);
  if (system_upgrade_start(update) == false)
  {
    os_printf("[OTA]Could not start upgrade\n");
    
    os_free(update->pespconn);
    os_free(update->url);
    os_free(update);
    return false;
  }
  else
  {
    os_printf("[OTA]Upgrading...\n");
    return true;
  }
}

#endif