// Copyright 2024 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

/**
 * The API of the library is intentionally made without having state
 * structure parameter. Everything is kept in one structure.
 * It's because right now, there is no plan to run multiple satellites in one device/executable.
 */

#include "satellite_priv.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

struct wsat_inst_priv wsat_priv;

int32_t wsat_run()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  int32_t ret = 0;
  int port, client_len;
  struct sockaddr_in serv_addr, client_addr;

  inst->connfd = inst->sockfd = -1;

  inst->sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (inst->sockfd < 0) {
    LOGE("Error opening socket");
    ret = -1;
    goto cleanup;
  }

  memset((char*)&serv_addr, 0, sizeof(serv_addr));
  port = 10700;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(port);

  if (bind(inst->sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    LOGE("Error on binding");
    ret = -2;
    goto cleanup;
  }

  if (listen(inst->sockfd, 1) < 0) {
    LOGE("Error on listen");
    ret = -3;
    goto cleanup;
  }

  LOGD("Server listening on port %d", port);
  client_len = sizeof(client_addr);

  bool exit = false;
  fd_set read_fds;

  while (!exit) {
    inst->connfd = accept(inst->sockfd, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
    if (inst->connfd < 0) {
      LOGE("Error on accept");
      continue;
    }
    LOGD("Client connected");

    FD_ZERO(&read_fds);
    FD_SET(inst->connfd, &read_fds);

    while (true) {
      ret = select(inst->connfd + 1, &read_fds, NULL, NULL, NULL);
      if (ret < 0) {
        LOGE("Select returned %d", ret);
        ret = -4;
        goto cleanup;
      }
      if (FD_ISSET(inst->connfd, &read_fds)) {
        // TODO: Mutex
        uint32_t bytes_to_read = DATA_BUFFER_SIZE - inst->data_buf_avail_bytes;
        ssize_t bytes_read = read(inst->connfd, inst->data_buf + inst->data_buf_avail_bytes, bytes_to_read);
        if (bytes_read == 0) {
          LOGD("Client disconnected");
          break;
        } else if (bytes_read < 0) {
          LOGE("Read error %d", errno);
          break;
        }
        inst->data_buf_avail_bytes += bytes_read;
        wsat_process_data();
      }
    }
    close(inst->connfd);
    inst->connfd = -1;
  }

cleanup:
  if (inst->connfd >= 0) close(inst->connfd);
  if (inst->sockfd >= 0) close(inst->sockfd);
  return ret;
}
