// SPDX-License-Identifier: BSD-2-Clause
#include "process_data.h"
#include "can_rx_data.h"
#include "gvret.h"
#include "gvret_protocol.h"
#include "gateway.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"

#define TX_BYTES 1024
#define RX_BYTES 128
#define FRAME_MARKS (TX_BYTES / 12 + 1)
#define SESSION_MASK 0x0FFFFFFFU

typedef struct
{
  uint8_t tx[TX_BYTES];
  uint8_t rx[RX_BYTES];
  uint16_t frame_ends[FRAME_MARKS];
  size_t tx_used, tx_sent, rx_used, rx_pos, marks_used, marks_sent;
  gw_gvret_protocol_t protocol;
  uint64_t connected_at, progress_at;
} GvretIo;

static DRAM_ATTR StaticQueue_t queue_control;
static QueueHandle_t frames;
static DRAM_ATTR uint8_t queue_storage[GW_GVRET_QUEUE_LENGTH * sizeof(gw_gvret_frame_t)];
static DRAM_ATTR GvretIo io_storage;
static GvretIo * io = &io_storage;
static int listener = -1, client = -1;
static uint32_t session, session_sequence;
// Atomics live in internal DRAM. Only TaskCan writes producer counters.
static uint32_t active_session, enqueued, dropped, queue_peak;
// Only the gvret task writes these, and is the only VS publisher of all stats.
static uint32_t sent, discarded, rejected_clients, ignored_commands, protocol_errors, tx_bytes;
static int last_error;

/*****************************************************************************/

static void Add(uint32_t * value, uint32_t amount)
{
  *value = amount > INT32_MAX - *value ? INT32_MAX : *value + amount;
}

/*****************************************************************************/

static void ProducerAdd(uint32_t * value)
{
  uint32_t old = __atomic_load_n(value, __ATOMIC_RELAXED);
  if (old < INT32_MAX)
    __atomic_store_n(value, old + 1, __ATOMIC_RELAXED);
}

/*****************************************************************************/

void gw_gvret_capture(uint32_t id, const uint8_t * data, size_t length,
                       bool extended, bool rtr, uint64_t timestamp_us)
{
  uint32_t token = __atomic_load_n(&active_session, __ATOMIC_ACQUIRE);
  if (!token || rtr || !data || length > 8 || id > (extended ? 0x1FFFFFFFU : 0x7FFU))
    return;
  gw_gvret_frame_t frame = {
    .timestamp_us = timestamp_us,
    .id = id | (extended ? 0x80000000U : 0),
    .tag = (token << 4) | length,
  };
  memcpy(frame.data, data, length);
  if (xQueueSend(frames, &frame, 0) != pdPASS)
  {
    ProducerAdd(&dropped);
    return;
  }
  ProducerAdd(&enqueued);
  uint32_t depth = uxQueueMessagesWaiting(frames);
  if (depth > __atomic_load_n(&queue_peak, __ATOMIC_RELAXED))
    __atomic_store_n(&queue_peak, depth, __ATOMIC_RELAXED);
}

/*****************************************************************************/

static void ClearQueue(void)
{
  Add(&discarded, uxQueueMessagesWaiting(frames));
  xQueueReset(frames);
}

/*****************************************************************************/

static void Disconnect(int error)
{
  __atomic_store_n(&active_session, 0, __ATOMIC_RELEASE);
  if (client >= 0)
    close(client);
  client = -1;
  if (error)
    last_error = error;
  Add(&discarded, io->marks_used - io->marks_sent);
  ClearQueue();
  memset(io, 0, sizeof(*io));
}

/*****************************************************************************/

static bool Nonblocking(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

/*****************************************************************************/

static void AcceptClients(uint64_t now)
{
  // Bounded accept work. A queued second connect must not become a hidden session.
  for (unsigned n = 0; n < 4; ++n)
  {
    int incoming = accept(listener, NULL, NULL);
    if (incoming < 0)
    {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        last_error = errno;
      return;
    }
    if (client >= 0)
    {
      close(incoming);
      Add(&rejected_clients, 1);
      continue;
    }
    int yes = 1;
    if (!Nonblocking(incoming) || setsockopt(incoming, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) != 0)
    {
      last_error = errno;
      close(incoming);
      continue;
    }
    setsockopt(incoming, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
    __atomic_store_n(&active_session, 0, __ATOMIC_RELEASE);
    ClearQueue();
    memset(io, 0, sizeof(*io));
    gw_gvret_protocol_reset(&io->protocol);
    session_sequence = (session_sequence + 1) & SESSION_MASK;
    if (!session_sequence)
      session_sequence = 1;
    session = session_sequence;
    client = incoming;
    io->connected_at = io->progress_at = now;
    ESP_LOGI("gvret", "Client connected; receive-only CAN0");
  }
}

/*****************************************************************************/

static void Flush(uint64_t now)
{
  if (client < 0 || io->tx_sent == io->tx_used)
    return;
  int n = send(client, io->tx + io->tx_sent, io->tx_used - io->tx_sent, 0);
  if (n > 0)
  {
    io->tx_sent += n;
    io->progress_at = now;
    Add(&tx_bytes, n);
    while (io->marks_sent < io->marks_used && io->frame_ends[io->marks_sent] <= io->tx_sent)
    {
      ++io->marks_sent;
      Add(&sent, 1);
    }
    if (io->tx_sent == io->tx_used)
    {
      io->tx_sent = io->tx_used = 0;
      io->marks_sent = io->marks_used = 0;
    }
  }
  else if (!n || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
    Disconnect(n ? errno : ECONNRESET);
}

/*****************************************************************************/

static void ReadCommands(uint64_t now)
{
  if (io->rx_pos == io->rx_used && TX_BYTES - io->tx_used >= GW_GVRET_REPLY_MAX)
  {
    int n = recv(client, io->rx, sizeof(io->rx), 0);
    if (!n || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
    {
      Disconnect(n ? errno : 0);
      return;
    }
    if (n > 0)
    {
      io->rx_pos = 0;
      io->rx_used = n;
    }
  }
  bool running = varstore_read(GW_VAR_CAN_STATE, 2000, GW_CAN_DISABLED) == GW_CAN_LISTENING;
  while (io->rx_pos < io->rx_used && TX_BYTES - io->tx_used >= GW_GVRET_REPLY_MAX)
  {
    uint8_t reply[GW_GVRET_REPLY_MAX];
    uint32_t old_ignored = io->protocol.ignored_commands;
    int n = gw_gvret_protocol_feed(&io->protocol, io->rx[io->rx_pos++], now, running, reply);
    if (n < 0)
    {
      Add(&protocol_errors, 1);
      Disconnect(EPROTO);
      return;
    }
    Add(&ignored_commands, io->protocol.ignored_commands - old_ignored);
    if (n)
    {
      if (!io->tx_used)
        io->progress_at = now;
      memcpy(io->tx + io->tx_used, reply, n);
      io->tx_used += n;
    }
    if (io->protocol.binary)
      __atomic_store_n(&active_session, session, __ATOMIC_RELEASE);
  }
}

/*****************************************************************************/

static void FillFrames(uint64_t now)
{
  for (unsigned n = 0; n < 32 && TX_BYTES - io->tx_used >= GW_GVRET_FRAME_MAX && io->marks_used < FRAME_MARKS; ++n)
  {
    gw_gvret_frame_t frame;
    if (xQueueReceive(frames, &frame, 0) != pdPASS)
      break;
    // A producer already in flight during disconnect may enqueue after reset.
    if ((frame.tag >> 4) != session)
    {
      Add(&discarded, 1);
      continue;
    }
    size_t length = gw_gvret_encode(&frame, io->tx + io->tx_used);
    if (!length)
    {
      Add(&discarded, 1);
      continue;
    }
    if (!io->tx_used)
      io->progress_at = now;
    io->tx_used += length;
    io->frame_ends[io->marks_used++] = io->tx_used;
  }
}

/*****************************************************************************/

static void ServiceStep(uint64_t now)
{
  AcceptClients(now);
  if (client < 0)
    return;
  if ((!io->protocol.binary && now - io->connected_at > 5000000U) ||
      (io->protocol.state && now - io->protocol.last_byte_us > 1000000U) ||
      (io->tx_used != io->tx_sent && now - io->progress_at > 2000000U))
  {
    Disconnect(ETIMEDOUT);
    return;
  }
  Flush(now);
  if (client < 0)
    return;
  ReadCommands(now);
  if (client < 0)
    return;
  if (io->protocol.binary && io->rx_pos == io->rx_used)
    FillFrames(now);
  Flush(now);
}

/*****************************************************************************/

static void Publish(void)
{
  varstore_write(GW_VAR_GVRET_STATE, client < 0 ? 1 : io->protocol.binary ? 3 : 2);
  varstore_write(GW_VAR_GVRET_QUEUED, uxQueueMessagesWaiting(frames));
  varstore_write(GW_VAR_GVRET_QUEUE_PEAK, __atomic_load_n(&queue_peak, __ATOMIC_RELAXED));
  varstore_write(GW_VAR_GVRET_ENQUEUED, __atomic_load_n(&enqueued, __ATOMIC_RELAXED));
  varstore_write(GW_VAR_GVRET_DROPPED, __atomic_load_n(&dropped, __ATOMIC_RELAXED));
  varstore_write(GW_VAR_GVRET_SENT, sent);
  varstore_write(GW_VAR_GVRET_DISCARDED, discarded);
  varstore_write(GW_VAR_GVRET_REJECTED_CLIENTS, rejected_clients);
  varstore_write(GW_VAR_GVRET_IGNORED_COMMANDS, ignored_commands);
  varstore_write(GW_VAR_GVRET_PROTOCOL_ERRORS, protocol_errors);
  varstore_write(GW_VAR_GVRET_TX_BYTES, tx_bytes);
  varstore_write(GW_VAR_GVRET_LAST_ERROR, last_error);
  varstore_write(GW_VAR_GVRET_QUEUE_CAPACITY, GW_GVRET_QUEUE_LENGTH);
  varstore_write(GW_VAR_GVRET_MEMORY_EXTERNAL, 0);
}

/*****************************************************************************/

static void GvretTask(void * unused)
{
  (void)unused;
  configASSERT(xPortGetCoreID() == GW_NETWORK_CORE);
  uint64_t publish_at = 0;
  for (;;)
  {
    uint64_t now = esp_timer_get_time();
    ServiceStep(now);
    if (now >= publish_at)
    {
      Publish();
      publish_at = now + 500000U;
    }
    vTaskDelay(pdMS_TO_TICKS(client < 0 ? 50 : 5));
  }
}

/*****************************************************************************/

esp_err_t gw_gvret_init(void)
{
  if (frames)
    return ESP_OK;
  frames = xQueueCreateStatic(GW_GVRET_QUEUE_LENGTH, sizeof(gw_gvret_frame_t), queue_storage, &queue_control);
  if (!frames)
    return ESP_ERR_NO_MEM;
  memset(io, 0, sizeof(*io));
  return ESP_OK;
}

/*****************************************************************************/

esp_err_t gw_gvret_start(void)
{
  configASSERT(xPortGetCoreID() == GW_NETWORK_CORE);
  if (!frames)
    return ESP_ERR_INVALID_STATE;
  if (listener >= 0)
    return ESP_ERR_INVALID_STATE;
  listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener < 0)
    goto fail;
  int yes = 1;
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(GW_GVRET_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
  if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0 ||
      !Nonblocking(listener) || bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listener, 1) != 0)
    goto fail;
  if (xTaskCreatePinnedToCore(GvretTask, "gvret", 6144, NULL, 3, NULL, GW_NETWORK_CORE) != pdPASS)
  {
    last_error = ENOMEM;
    goto close_listener;
  }
  ESP_LOGI("gvret", "SavvyCAN TCP %d, CPU0, single client, RX only", GW_GVRET_PORT);
  return ESP_OK;
fail:
  last_error = errno;
close_listener:
  if (listener >= 0)
    close(listener);
  listener = -1;
  varstore_write(GW_VAR_GVRET_STATE, 4);
  varstore_write(GW_VAR_GVRET_LAST_ERROR, last_error);
  return ESP_FAIL;
}
