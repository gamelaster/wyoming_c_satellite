#include <wyoming/satellite.h>
#include <stdio.h>
#include <stdarg.h>

int main(int argc, char** argv)
{
  wsat_run();
}

void debug_print(char type, const char *format, ...) {
  va_list args;
  va_start(args, format);
  printf("[%c] ", type);
  vprintf(format, args);
  printf("\n");
  va_end(args);
}