// Native boundary adapter: actual protocol/server/VS; mock only IDF/OS facilities.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "varstore.h"
#include "process_data.h"
#include "can_rx_data.h"
#include "gvret_protocol.h"
#include "gvret.h"
#define DRAM_ATTR
#define GW_NETWORK_CORE 0
#define ESP_LOGI(...) ((void)0)
#define configASSERT(x) do { if (!(x)) __builtin_trap(); } while (0)
#define ESP_FAIL (-1)
#define ESP_ERR_INVALID_STATE 0x103
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define AF_INET 2
#define SOCK_STREAM 1
#define IPPROTO_TCP 6
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_KEEPALIVE 9
#define TCP_NODELAY 1
#define INADDR_ANY 0
#define F_GETFL 1
#define F_SETFL 2
#define O_NONBLOCK 0x800
#define EAGAIN 11
#define EWOULDBLOCK 11
#define EINTR 4
#define ECONNRESET 104
#define ETIMEDOUT 110
#define EPROTO 71
#define ENOMEM 12
static int test_errno;
#define errno test_errno
typedef void * TaskHandle_t;
typedef struct { uint32_t s_addr; } InAddr;
struct sockaddr { uint16_t family; uint8_t data[14]; };
struct sockaddr_in { uint16_t sin_family,sin_port; InAddr sin_addr; };
static uint16_t htons(uint16_t x) { return (uint16_t)(x<<8 | x>>8); }
static uint32_t htonl(uint32_t x) { return (uint32_t)htons((uint16_t)x)<<16 | htons((uint16_t)(x>>16)); }

typedef int (*IoHook)(int op,int fd,void *data,int length,int arg);
static IoHook hook;
static int Call(int op,int fd,void *data,int length,int arg)
{
  int result=hook(op,fd,data,length,arg);
  if(result<0) { test_errno=-result;return -1; }
  return result;
}
static int socket(int a,int b,int c) { return Call(1,a,NULL,b,c); }
static int bind(int fd,struct sockaddr *p,size_t n) { return Call(2,fd,p,n,0); }
static int listen(int fd,int n) { return Call(3,fd,NULL,n,0); }
static int accept(int fd,void *a,void *b) { (void)a;(void)b;return Call(4,fd,NULL,0,0); }
static int send(int fd,const void *p,size_t n,int flags) { return Call(5,fd,(void *)p,n,flags); }
static int recv(int fd,void *p,size_t n,int flags) { return Call(6,fd,p,n,flags); }
static int close(int fd) { return Call(7,fd,NULL,0,0); }
static int setsockopt(int fd,int level,int option,const void *p,size_t n) { (void)p;(void)n;return Call(8,fd,NULL,level,option); }
static int fcntl(int fd,int cmd,int flags) { return Call(cmd==F_GETFL?9:10,fd,NULL,flags,0); }

typedef struct { unsigned capacity,item,head,tail,count;uint8_t *data; } StaticQueue_t;
typedef StaticQueue_t * QueueHandle_t;
static QueueHandle_t xQueueCreateStatic(unsigned capacity,unsigned item,uint8_t *data,StaticQueue_t *control)
{
  *control=(StaticQueue_t){.capacity=capacity,.item=item,.data=data};return control;
}
static int xQueueSend(QueueHandle_t q,const void *p,unsigned wait)
{
  configASSERT(wait==0);
  if(q->count==q->capacity)return 0;
  memcpy(q->data+q->tail*q->item,p,q->item);q->tail=(q->tail+1)%q->capacity;++q->count;return pdPASS;
}
static int xQueueReceive(QueueHandle_t q,void *p,unsigned wait)
{
  configASSERT(wait==0);
  if(!q->count)return 0;
  memcpy(p,q->data+q->head*q->item,q->item);q->head=(q->head+1)%q->capacity;--q->count;return pdPASS;
}
static unsigned uxQueueMessagesWaiting(QueueHandle_t q) { return q->count; }
static void xQueueReset(QueueHandle_t q) { q->head=q->tail=q->count=0; }
static uint64_t now_us;
int64_t esp_timer_get_time(void) { return now_us; }
uint32_t TimerGetTs(void) { return (uint32_t)now_us; }
static int xPortGetCoreID(void) { return 0; }
static void vTaskDelay(unsigned ticks) { (void)ticks; }
static unsigned task_core,task_priority,task_stack,task_starts;
static int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle,int core)
{
  (void)fn;(void)name;(void)arg;(void)handle;
  task_core=core;task_priority=priority;task_stack=stack;++task_starts;return pdPASS;
}



/* ACTUAL_GVRET */

void gvret_test_hook(IoHook fn) { hook=fn; }
void gvret_test_reset(void)
{
  if(frames&&io)Disconnect(0);
  if(listener>=0)close(listener);
  listener=-1;
  enqueued=dropped=queue_peak=sent=discarded=rejected_clients=ignored_commands=protocol_errors=tx_bytes=0;
  active_session=session=session_sequence=0;last_error=0;now_us=0;task_starts=0;
  gw_process_data_init();varstore_write(GW_VAR_CAN_STATE,GW_CAN_LISTENING);
}
void gvret_test_poll(uint64_t time) { now_us=time;ServiceStep(time);Publish(); }
int gvret_test_stat(const char *name)
{
  for(size_t i=0;i<varstore_count();++i) {
    const varstore_definition_t *d=varstore_definition(i);
    if(d&&!strcmp(d->name,name))return varstore_read(i,0,-1);
  }
  return -1;
}
unsigned gvret_test_token(void) { return active_session; }
void gvret_test_stale(unsigned token)
{
  gw_gvret_frame_t f={.id=0x666,.tag=(token<<4)|1,.data={0xAA}};
  xQueueSend(frames,&f,0);
}
unsigned gvret_test_config(unsigned i)
{
  unsigned v[]={task_starts,task_core,task_priority,task_stack,sizeof(gw_gvret_frame_t),(unsigned)(sizeof(queue_storage)+sizeof(io_storage)),(unsigned)sizeof(queue_storage)};return v[i];
}
