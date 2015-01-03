#include <stdio.h>
#include <stdint.h>

extern uint32_t rtlsdr_rpc_get_device_count(void);
extern const char* rtlsdr_rpc_get_device_name(uint32_t);

int main(int ac, char** av)
{
  uint32_t i;
  uint32_t n;

  n = rtlsdr_rpc_get_device_count();
  printf("count: %u\n", n);

  for (i = 0; i != n; ++i)
  {
    const char* const s = rtlsdr_rpc_get_device_name(i);
    printf("name[%u]: %s\n", i, s);
  }

  return 0;
}
