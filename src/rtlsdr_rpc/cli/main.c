#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>

typedef struct rtlsdr_rpc_dev rtlsdr_rpc_dev_t;

extern uint32_t rtlsdr_rpc_get_device_count(void);
extern const char* rtlsdr_rpc_get_device_name(uint32_t);
extern const char* rtlsdr_rpc_get_device_name(uint32_t);
extern int rtlsdr_rpc_open(rtlsdr_rpc_dev_t**, uint32_t);
extern int rtlsdr_rpc_close(rtlsdr_rpc_dev_t*);
extern int rtlsdr_rpc_set_xtal_freq(rtlsdr_rpc_dev_t*, uint32_t, uint32_t);
extern int rtlsdr_rpc_get_xtal_freq(rtlsdr_rpc_dev_t*, uint32_t*, uint32_t *);
extern int rtlsdr_rpc_set_center_freq(rtlsdr_rpc_dev_t*, uint32_t);
extern uint32_t rtlsdr_rpc_get_center_freq(rtlsdr_rpc_dev_t*);
extern int rtlsdr_rpc_set_sample_rate(rtlsdr_rpc_dev_t*, uint32_t);
extern uint32_t rtlsdr_rpc_get_sample_rate(rtlsdr_rpc_dev_t*);
extern int rtlsdr_rpc_reset_buffer(rtlsdr_rpc_dev_t*);
extern int rtlsdr_rpc_cancel_async(rtlsdr_rpc_dev_t*);
typedef void (*rtlsdr_rpc_read_async_cb_t)
(unsigned char*, uint32_t, void*);
extern int rtlsdr_rpc_read_async
(
 rtlsdr_rpc_dev_t*,
 rtlsdr_rpc_read_async_cb_t, void*,
 uint32_t, uint32_t
);
extern int rtlsdr_rpc_cancel_async(rtlsdr_rpc_dev_t*);

static void async_cb(unsigned char* buf, uint32_t len, void* p)
{
  printf("async_cb %u\n", len);
}

static rtlsdr_rpc_dev_t* gdev;
static void on_sigint(int x)
{
  rtlsdr_rpc_cancel_async(gdev);
}

int main(int ac, char** av)
{
  rtlsdr_rpc_dev_t* dev;
  uint32_t i;
  uint32_t n;

  n = rtlsdr_rpc_get_device_count();
  printf("count: %u\n", n);

  for (i = 0; i != n; ++i)
  {
    const char* const s = rtlsdr_rpc_get_device_name(i);
    printf("name[%u]: %s\n", i, s);

    if (rtlsdr_rpc_open(&dev, i))
    {
      printf("open error\n");
      continue ;
    }

    rtlsdr_rpc_reset_buffer(dev);
    rtlsdr_rpc_set_center_freq(dev, 120000000);
    rtlsdr_rpc_set_sample_rate(dev, 300000);
    gdev = dev;
    signal(SIGINT, on_sigint);
    rtlsdr_rpc_read_async(dev, async_cb, NULL, 8, 64 * 1024);
    rtlsdr_rpc_close(dev);
  }

  return 0;
}
