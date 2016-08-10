#ifndef SYNC_H
#define SYNC_H
#include "os_type.h"

typedef enum {
  sync_type_both = 0,
  sync_type_time,
  sync_type_full,
  sync_type_position
} sync_type_t;


void sync_init();

#ifdef SYNC_HTTP
bool json_put_char(char c);
bool json_put_string(char *s);
#endif

void sync_sync();

#endif