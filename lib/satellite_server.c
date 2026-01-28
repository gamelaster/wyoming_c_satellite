// Copyright 2026 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "satellite_priv.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define WSAT_SEND_TIMEOUT_MS 250

static bool is_stop_requested()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_server* server = &inst->server;
  PLAT_MUTEX_LOCK(&server->state_mutex);
  const bool stop_requested = server->stop_requested;
  PLAT_MUTEX_UNLOCK(&server->state_mutex);
  return stop_requested;
}

static bool wsat_errno_is_retry(int err)
{
  return err == EINTR;
}

static bool wsat_errno_is_fatal_listener(int err)
{
  return err == EBADF || err == EINVAL;
}

static bool wsat_errno_is_accept_transient(int err)
{
  return err == ECONNABORTED ||
         err == EPROTO ||
         err == ENOPROTOOPT ||
         err == EAGAIN ||
         err == EWOULDBLOCK;
}

static bool wsat_errno_is_conn_drop(int err)
{
  return err == ECONNRESET ||
         err == ECONNABORTED ||
         err == ETIMEDOUT ||
         err == EPIPE ||
         err == ENOTCONN ||
         err == ENETRESET ||
         err == ENETDOWN ||
         err == EHOSTUNREACH;
}

int32_t wsat_server_run()
{
  int32_t res;
  int32_t ret = WSAT_OK;
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_server* server = &inst->server;
  struct sockaddr_in serv_addr, client_addr;
  int port, client_len;
  int sockfd, connfd;

  PLAT_MUTEX_LOCK(&server->state_mutex);
  server->stop_requested = false;
  server->sockfd = sockfd = -1;
  server->connfd = connfd = -1;
  PLAT_MUTEX_UNLOCK(&server->state_mutex);

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    LOGE("socket() failed");
    ret = -WSAT_ERROR_SOCKET;
    goto cleanup;
  }
  PLAT_MUTEX_LOCK(&server->state_mutex);
  server->sockfd = sockfd;
  PLAT_MUTEX_UNLOCK(&server->state_mutex);

  memset((char*)&serv_addr, 0, sizeof(serv_addr));
  port = 10700; // TODO: From config
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(port);

  const int enable = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
    LOGE("setsockopt(SO_REUSEADDR) failed");
    ret = -WSAT_ERROR_SOCKET;
    goto cleanup;
  }

  if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    LOGE("bind() failed, err: %d", errno);
    ret = -WSAT_ERROR_SOCKET;
    goto cleanup;
  }

  if (listen(sockfd, 1) < 0) {
    LOGE("listen() failed, err: %d", errno);
    ret = -WSAT_ERROR_SOCKET;
    goto cleanup;
  }

  LOGD("Server listening on port %d", port);

  // For graceful shutdowns, we use selects + timeouts. Pipes would work too, but there are no pipes in embedded env.
  fd_set read_fds;
  struct wsat_decoded_event evt;
  struct wsat_event_decoder* dec = &server->decoder;

  while (true) {
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);
    struct timeval accept_timeout = {0, 250 * 1000};
    res = select(sockfd + 1, &read_fds, NULL, NULL, &accept_timeout);
    if (res < 0) {
      if (wsat_errno_is_retry(errno)) continue;
      LOGE("select() failed");
      ret = -WSAT_ERROR_SOCKET;
      goto cleanup;
    }
    if (is_stop_requested()) goto cleanup;
    if (res == 0 || !FD_ISSET(sockfd, &read_fds)) continue; // Timeout or no new connection
    client_len = sizeof(client_addr);
    connfd = accept(sockfd, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
    if (connfd < 0) {
      if (wsat_errno_is_retry(errno) || wsat_errno_is_accept_transient(errno)) continue;
      if (wsat_errno_is_fatal_listener(errno)) {
        LOGE("accept() failed");
        ret = -WSAT_ERROR_SOCKET;
        goto cleanup;
      }
      LOGE("accept() failed");
      ret = -WSAT_ERROR_SOCKET;
      goto cleanup;
    }
    LOGD("Client connected");
    PLAT_MUTEX_LOCK(&server->state_mutex);
    server->connfd = connfd;
    PLAT_MUTEX_UNLOCK(&server->state_mutex);
    wsat_event_decoder_reset(dec);

    while (true) {
      FD_ZERO(&read_fds);
      FD_SET(connfd, &read_fds);
      struct timeval read_timeout = {0, 250 * 1000};
      res = select(connfd + 1, &read_fds, NULL, NULL, &read_timeout);
      if (res < 0) {
        if (wsat_errno_is_retry(errno)) continue;
        if (wsat_errno_is_conn_drop(errno)) {
          ret = 0;
          break;
        }
        LOGE("select() failed");
        ret = -WSAT_ERROR_SOCKET;
        break;
      }
      if (is_stop_requested()) break;
      if (res == 0 || !FD_ISSET(connfd, &read_fds)) continue;
      // We received data!
      uint8_t* read_buffer;
      uint32_t capacity = wsat_event_decoder_buffer_get(dec, &read_buffer);
      const ssize_t bytes_read = read(connfd, read_buffer, capacity);
      if (bytes_read == 0) {
        LOGD("Client disconnected");
        break;
      }
      if (bytes_read < 0) {
        if (wsat_errno_is_retry(errno)) continue;
        if (wsat_errno_is_conn_drop(errno)) {
          ret = 0;
          break;
        }
        LOGD("read() failed: %d", errno);
        ret = -WSAT_ERROR_SOCKET;
        break;
      }
      wsat_event_decoder_buffer_advance(dec, bytes_read);
      uint32_t dec_res = 0;
      do {
        dec_res = wsat_event_decoder_next(dec, &evt);
        if (dec_res == 1) {
#if 1
          if (evt.flags & WSAT_DECODED_EVENT_FLAG_BEGIN) {
            LOGD("Got event \"%s\"", evt.header.type);
          }
#endif
          wsat_event_handle(&evt);
          if (evt.flags & WSAT_DECODED_EVENT_FLAG_END) {
            wsat_decoded_event_free(&evt);
          }
        }
      } while (dec_res != 0);
    }
    PLAT_MUTEX_LOCK(&server->state_mutex);
    close(connfd);
    server->connfd = connfd = -1;
    PLAT_MUTEX_UNLOCK(&server->state_mutex);
    if (ret < 0) break;
  }

cleanup:
  PLAT_MUTEX_LOCK(&server->state_mutex);
  if (sockfd >= 0) close(sockfd);
  server->sockfd = sockfd = -1;
  PLAT_MUTEX_UNLOCK(&server->state_mutex);
  return ret;
}

static int32_t wsat_send_all(int fd, const uint8_t* buffer, size_t length, int timeout_ms)
{
  size_t sent = 0;
  const size_t max_chunk = 4096;
  while (sent < length) {
    if (is_stop_requested()) return -WSAT_ERROR_SOCKET; // TODO: Maybe change to something else

    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(fd, &write_fds);
    struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    const int sel = select(fd + 1, NULL, &write_fds, NULL, &tv);
    if (sel < 0) {
      if (wsat_errno_is_retry(errno)) continue;
      return -WSAT_ERROR_SOCKET;
    }
    if (sel == 0 || !FD_ISSET(fd, &write_fds)) {
      continue;
    }

    size_t remaining = length - sent;
    size_t chunk = remaining > max_chunk ? max_chunk : remaining;
    const ssize_t res = send(fd, buffer + sent, chunk, MSG_NOSIGNAL);
    if (res > 0) {
      sent += (size_t)res;
      continue;
    }
    if (res < 0 && (wsat_errno_is_retry(errno) || errno == EAGAIN || errno == EWOULDBLOCK)) {
      continue;
    }
    return -WSAT_ERROR_SOCKET;
  }
  return WSAT_OK;
}

int32_t wsat_event_send(struct wsat_event* evt)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_server* server = &inst->server;
  int32_t ret = WSAT_OK;
  int connfd = -1;

  PLAT_MUTEX_LOCK(&server->state_mutex);
  if (server->connfd < 0 || server->stop_requested) {
    PLAT_MUTEX_UNLOCK(&server->state_mutex);
    return -WSAT_ERROR_SAT_DISCONNECTED;
  }
  connfd = server->connfd;
  PLAT_MUTEX_UNLOCK(&server->state_mutex);

  // TODO: Instead of using malloc in cJSON, we can print to our own buffers instead.

  char* data_json = NULL;
  size_t data_json_length = 0;
  char* header_json = NULL;
  size_t header_json_length = 0;
  if (evt == NULL || evt->header == NULL) return -WSAT_ERROR_SOCKET;
  if (evt->data != NULL) {
    data_json = cJSON_PrintUnformatted(evt->data);
    if (data_json == NULL) {
      ret = -WSAT_ERROR_SOCKET;
      goto cleanup;
    }
    data_json_length = strlen(data_json);
    cJSON_DeleteItemFromObjectCaseSensitive(evt->header, "data_length");
    cJSON_AddItemToObject(evt->header, "data_length", cJSON_CreateNumber((double)data_json_length));
  } else {
    cJSON_DeleteItemFromObjectCaseSensitive(evt->header, "data_length");
  }
  if (evt->payload != NULL) {
    cJSON_DeleteItemFromObjectCaseSensitive(evt->header, "payload_length");
    cJSON_AddItemToObject(evt->header, "payload_length", cJSON_CreateNumber(evt->payload_length));
  } else {
    cJSON_DeleteItemFromObjectCaseSensitive(evt->header, "payload_length");
  }
  header_json = cJSON_PrintUnformatted(evt->header);
  if (header_json == NULL) {
    ret = -WSAT_ERROR_SOCKET;
    goto cleanup;
  }
  header_json_length = strlen(header_json);
  // We are abusing the fact that every string ends with \0
  header_json[header_json_length] = '\n';

  PLAT_MUTEX_LOCK(&server->send_mutex);
  if (wsat_send_all(connfd, (const uint8_t*)header_json, header_json_length + 1, WSAT_SEND_TIMEOUT_MS) < 0) {
    ret = -WSAT_ERROR_SOCKET;
    goto send_cleanup;
  }
  if (data_json != NULL) {
    if (wsat_send_all(connfd, (const uint8_t*)data_json, data_json_length, WSAT_SEND_TIMEOUT_MS) < 0) {
      ret = -WSAT_ERROR_SOCKET;
      goto send_cleanup;
    }
  }
  if (evt->payload != NULL && evt->payload_length > 0) {
    if (wsat_send_all(connfd, evt->payload, evt->payload_length, WSAT_SEND_TIMEOUT_MS) < 0) {
      ret = -WSAT_ERROR_SOCKET;
      goto send_cleanup;
    }
  }
send_cleanup:
  PLAT_MUTEX_UNLOCK(&server->send_mutex);
cleanup:
  free(header_json);
  if (data_json != NULL) free(data_json);
  return ret;
}

void wsat_event_free(struct wsat_event* evt, bool free_payload)
{
  if (evt->header != NULL) cJSON_Delete(evt->header);
  if (evt->data != NULL) cJSON_Delete(evt->data);
  if (free_payload && evt->payload != NULL) free(evt->payload);
}
