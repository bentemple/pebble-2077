#include <pebble.h>
#include <ctype.h>
#include "utils.h"

void str_to_upper(char *str) {
  char *s = str;
  while (*s) {
    *s = toupper((unsigned char) *s);
    s++;
  }
}
