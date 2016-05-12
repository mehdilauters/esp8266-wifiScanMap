#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
// #include "uart.h"
// #include "driver/uart_register.h"
#include "user_config.h"
#include "user_json.h"
#include "scanmap.h"
#include "sync.h"

#include "user_interface.h"


#define user_procTaskPrio        0
#define user_procTaskQueueLen    1

os_event_t    user_procTaskQueue[user_procTaskQueueLen];
static void user_procTask(os_event_t *events);

static volatile os_timer_t second_timer;

volatile uint32_t seconds = 0;

void user_rf_pre_init(void) {
}


void second_cb(void *arg) {
  //TODO LOCK
  seconds ++;
}

uint32_t get_seconds() {
  return seconds;
}

void toggle_led() {
  //Do blinky stuff
  if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & BIT2)
  {
    //Set GPIO2 to LOW
    gpio_output_set(0, BIT2, BIT2, 0);
  }
  else
  {
    //Set GPIO2 to HIGH
    gpio_output_set(BIT2, 0, BIT2, 0);
  } 
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





//Do nothing function
static void ICACHE_FLASH_ATTR
user_procTask(os_event_t *events)
{
    os_delay_us(10);
}



void ICACHE_FLASH_ATTR init_done(void) {
//   os_printf("\n\nSDK version:%s\n", system_get_sdk_version());
  os_printf("Sdk version %s\n", system_get_sdk_version());
  scanmap_init();
  sync_init();
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
    
  
  os_timer_disarm(&second_timer);
  os_timer_setfn(&second_timer, (os_timer_func_t *) second_cb, NULL);
  os_timer_arm(&second_timer, 1000, 1);
  
  
  //Start os task
  system_os_task(user_procTask, user_procTaskPrio,user_procTaskQueue, user_procTaskQueueLen);
  
  
  os_printf("Hello\n\r");
}
