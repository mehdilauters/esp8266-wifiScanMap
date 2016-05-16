#ifndef BASE64_H
#define BASE64_H
//  https://github.com/mziwisky/esp8266-dev/blob/master/esphttpd/user/base64.h
 
 int base64_decode(size_t in_len, const char *in, size_t out_len, unsigned char *out);
 int base64_encode(size_t in_len, const unsigned char *in, size_t out_len, char *out);
 
#endif