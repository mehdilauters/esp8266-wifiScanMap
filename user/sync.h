#ifndef SYNC_H
#define SYNC_H
#include "os_type.h"


void sync_init();

#ifdef SYNC_HTTP
bool json_put_char(char c);
bool json_put_string(char *s);
#endif

#endif