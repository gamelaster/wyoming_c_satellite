// Copyright 2024 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

/**
 * The API of the library is intentionally made without having state
 * structure parameter. Everything is kept in one structure.
 * It's because right now, there is no plan to run multiple satellites in one device/executable.
 */

#include "satellite_priv.h"
#include "wyoming/satellite.h"

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
  inst->mode = &wsat_mode_always_stream; // TODO:

  struct component_entry
  {
    const char* name;
    int32_t (* init_fn)();
    int32_t (* destroy_fn)();
  } components[] = {
    {
      "mode",
      inst->mode->init_fn,
      inst->mode->destroy_fn
    },
    {
      "microphone",
      inst->mic ? inst->mic->init_fn : NULL,
      inst->mic ? inst->mic->destroy_fn : NULL,
    },
    {
      "speaker",
      inst->snd ? inst->snd->init_fn : NULL,
      inst->snd ? inst->snd->destroy_fn : NULL,
    }
  };

  for (int i = 0; i < 3; i++) {
    struct component_entry cmp = components[i];
    if (cmp.init_fn != NULL) {
      if (cmp.init_fn() < 0) {
        LOGE("Failed to initialize %s", cmp.name);
        ret = -5;
        goto cleanup;
      }
    }
  }

  if (PLAT_MUTEX_CREATE(&inst->is_streaming_mutex) != 0) {
    LOGD("Failed to initialize mutex");
    return -1;
  }

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

  const int enable = 1;
  if (setsockopt(inst->sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
    LOGE("setsockopt(SO_REUSEADDR) failed");
    ret = -1;
    goto cleanup;
  }

  if (bind(inst->sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    LOGE("Error on binding, err: %d", errno);
    ret = -2;
    goto cleanup;
  }

  if (listen(inst->sockfd, 1) < 0) {
    LOGE("Error on listen");
    ret = -3;
    goto cleanup;
  }

  if (PLAT_MUTEX_CREATE(&inst->conn_mutex) != 0) {
    LOGE("Failed to create mutex");
    ret = -4;
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
      break;
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
        uint32_t bytes_to_read = DATA_BUFFER_SIZE - inst->data_buf_avail_bytes;
        PLAT_MUTEX_LOCK(&inst->conn_mutex);
        ssize_t bytes_read = read(inst->connfd, inst->data_buf + inst->data_buf_avail_bytes, bytes_to_read);
        PLAT_MUTEX_UNLOCK(&inst->conn_mutex);
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

    // Stop the streams on disconnect
    if (inst->mic != NULL && inst->mic->stop_stream_fn != NULL) {
      inst->mic->stop_stream_fn();
    }
    if (inst->snd != NULL && inst->snd->stop_stream_fn != NULL) {
      inst->snd->stop_stream_fn();
    }
    // TODO: DO this differently
    PLAT_MUTEX_LOCK(&inst->is_streaming_mutex);
    inst->is_streaming = false;
    PLAT_MUTEX_UNLOCK(&inst->is_streaming_mutex);
  }

cleanup:
  PLAT_MUTEX_DESTROY(&inst->conn_mutex);
  if (inst->connfd >= 0) close(inst->connfd);
  if (inst->sockfd >= 0) close(inst->sockfd);
  inst->connfd = inst->sockfd = -1;
  PLAT_MUTEX_DESTROY(&inst->is_streaming_mutex);
  for (int i = 0; i < 3; i++) {
    struct component_entry cmp = components[i];
    if (cmp.destroy_fn != NULL) {
      cmp.destroy_fn();
    }
  }
  return ret;
}

int32_t wsat_packet_send(struct wsat_packet pkt)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  int32_t ret = 0;
  ssize_t res;

  char* data_json = NULL;
  size_t data_json_length;
  if (pkt.data != NULL) {
    data_json = cJSON_PrintUnformatted(pkt.data);
    data_json_length = strlen(data_json);
    cJSON_AddItemToObject(pkt.header, "data_length", cJSON_CreateNumber((double)data_json_length));
  }
  if (pkt.payload != NULL) {
    cJSON_AddItemToObject(pkt.header, "payload_length", cJSON_CreateNumber(pkt.payload_length));
  }
  char* header_json = cJSON_PrintUnformatted(pkt.header);
  size_t header_json_length = strlen(header_json);
  // We are abusing the fact that every string ends with \0
  header_json[header_json_length] = '\n';

  PLAT_MUTEX_LOCK(&inst->conn_mutex);
  res = send(inst->connfd, header_json, header_json_length + 1, 0);
  if (res != header_json_length + 1) {
    LOGE("Failed to send header, err %d", errno);
    ret = -1;
    goto cleanup;
  }
  if (pkt.data != NULL) {
    res = send(inst->connfd, data_json, data_json_length, 0);
    if (res != data_json_length) {
      LOGE("Failed to send data, err %d", errno);
      ret = -1;
      goto cleanup;
    }
  }
  if (pkt.payload != NULL) {
    res = send(inst->connfd, pkt.payload, pkt.payload_length, 0);
    if (res != pkt.payload_length) {
      LOGE("Failed to send payload, err %d", errno);
      ret = -1;
      goto cleanup;
    }
  }
cleanup:
  free(header_json);
  if (pkt.data != NULL) free(data_json);
  PLAT_MUTEX_UNLOCK(&inst->conn_mutex);
  return ret;
}

void wsat_packet_free(struct wsat_packet pkt, bool free_payload)
{
  if (pkt.header != NULL) cJSON_Delete(pkt.header);
  if (pkt.data != NULL) cJSON_Delete(pkt.data);
  if (free_payload && pkt.payload != NULL) free(pkt.payload);
}

void wsat_mic_set(struct wsat_microphone* mic)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  inst->mic = mic;
}

void wsat_snd_set(struct wsat_sound* snd)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  inst->snd = snd;
}

void wsat_mic_write_data(uint8_t* data, uint32_t length)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  PLAT_MUTEX_LOCK(&inst->is_streaming_mutex);
  bool is_streaming = inst->is_streaming;
  PLAT_MUTEX_UNLOCK(&inst->is_streaming_mutex);
  if (!is_streaming) return;

  cJSON* header = cJSON_CreateObject();
  cJSON_AddStringToObject(header, "type", "audio-chunk");
  cJSON_AddStringToObject(header, "version", "1.5.2");

  cJSON* pkt_data = cJSON_CreateObject();
  cJSON_AddNumberToObject(pkt_data, "rate", inst->mic->rate);
  cJSON_AddNumberToObject(pkt_data, "width", inst->mic->width);
  cJSON_AddNumberToObject(pkt_data, "channels", inst->mic->channels);
  cJSON_AddNumberToObject(pkt_data, "timestamp", 4407203886274); // TODO:

  struct wsat_packet res_pkt = {
    .header = header,
    .data = pkt_data,
    .payload = data,
    .payload_length = length
  };
  wsat_packet_send(res_pkt);
  wsat_packet_free(res_pkt, false);
}

void wsat_stop()
{
  //struct wsat_inst_priv* inst = &wsat_priv;
  //close(inst->sockfd);
  //PLAT_MUTEX_LOCK(&inst->conn_mutex);
  //if (inst->connfd != -1) close(inst->connfd);
  //PLAT_MUTEX_UNLOCK(&inst->conn_mutex);

}

int32_t wsat_send_run_pipeline(const char* pipeline_name)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  const char* start_stage, * end_stage;
  bool restart_on_end = false;

  if (inst->mode->type == WSAT_MODE_WAKE_STREAM) {
    // Local wake word detection
    start_stage = "asr";
    restart_on_end = false;
  } else {
    // Remote wake word detection
    start_stage = "wake";
    restart_on_end = true; // TODO: VAD
  }

  if (inst->snd != NULL) {
    // When we have speaker available, play TTS response
    end_stage = "tts";
  } else {
    end_stage = "handle";
  }

  cJSON* header = cJSON_CreateObject();
  cJSON_AddStringToObject(header, "type", "run-pipeline");
  cJSON_AddStringToObject(header, "version", "1.5.2");

  cJSON* data = cJSON_CreateObject();
  if (pipeline_name != NULL) cJSON_AddStringToObject(data, "name", pipeline_name);
  cJSON_AddStringToObject(data, "start_stage", start_stage);
  cJSON_AddStringToObject(data, "end_stage", end_stage);
  cJSON_AddBoolToObject(data, "restart_on_end", restart_on_end);

  struct wsat_packet res_pkt = {
    .header = header,
    .data = data
  };
  wsat_packet_send(res_pkt);
  wsat_packet_free(res_pkt, true);
  return 0;
}
