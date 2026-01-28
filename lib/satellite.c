// Copyright 2026 Marek Kraus (@gamelaster / @gamiee)
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

int32_t wsat_init()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_server* server = &inst->server;
  memset(inst, 0, sizeof(struct wsat_inst_priv));
  server->connfd = -1;
  server->sockfd = -1;
  PLAT_MUTEX_CREATE(&server->state_mutex); // TODO: Error check
  PLAT_MUTEX_CREATE(&server->send_mutex); // TODO: Error check
  return 0;
}

void wsat_destroy()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_server* server = &inst->server;
  PLAT_MUTEX_DESTROY(&server->send_mutex);
  PLAT_MUTEX_DESTROY(&server->state_mutex);
}

int32_t wsat_run()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  int32_t res = WSAT_OK;
  if (inst->wake != NULL) {
    inst->mode = &wsat_mode_wake_stream;
  } else {
    inst->mode = &wsat_mode_always_stream;
  }

  memset(inst->components, 0, sizeof(inst->components));
  inst->components[0] = (struct wsat_component*)inst->mode;
  inst->components[1] = (struct wsat_component*)inst->snd;
  inst->components[2] = (struct wsat_component*)inst->mic;
  inst->components[3] = (struct wsat_component*)inst->wake;
  for (int i = 0; i < ARRAY_LENGTH(inst->components); i++) {
    struct wsat_component* comp = inst->components[i];
    if (comp != NULL && comp->init_fn != NULL && !comp->is_init) {
      res = comp->init_fn();
      if (res < 0) {
        LOGE("Component #%d failed to init: %d", i, res);
        goto cleanup;
      }
      comp->is_init = true;
    }
  }
  res = wsat_server_run();
cleanup:
  for (int i = 0; i < ARRAY_LENGTH(inst->components); i++) {
    struct wsat_component* comp = inst->components[i];
    if (comp != NULL) {
      if (comp->destroy_fn != NULL && comp->is_init) {
        comp->destroy_fn();
      }
      comp->is_init = false;
    }
  }
  return res;
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

void wsat_wake_set(struct wsat_wake* wake)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  inst->wake = wake;
}

void wsat_stop()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_server* server = &inst->server;
  PLAT_MUTEX_LOCK(&server->state_mutex);
  server->stop_requested = true;
  PLAT_MUTEX_UNLOCK(&server->state_mutex);
  // TODO: Semaphore to wait for shutdown
}

int32_t wsat_run_pipeline_send(const char* pipeline_name)
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

  struct wsat_event res_pkt = {
    .header = header,
    .data = data
  };
  int32_t res = wsat_event_send(&res_pkt);
  wsat_event_free(&res_pkt, false);
  return res;
}

int32_t wsat_audio_chunk_send(uint8_t* data, uint32_t length)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  cJSON* header = cJSON_CreateObject();
  cJSON_AddStringToObject(header, "type", "audio-chunk");
  cJSON_AddStringToObject(header, "version", "1.5.2");

  cJSON* evt_data = cJSON_CreateObject();
  cJSON_AddNumberToObject(evt_data, "rate", inst->mic->rate);
  cJSON_AddNumberToObject(evt_data, "width", inst->mic->width);
  cJSON_AddNumberToObject(evt_data, "channels", inst->mic->channels);
  cJSON_AddNumberToObject(evt_data, "timestamp", 4407203886274); // TODO:

  struct wsat_event res_pkt = {
    .header = header,
    .data = evt_data,
    .payload = data,
    .payload_length = length
  };
  int32_t res = wsat_event_send(&res_pkt);
  wsat_event_free(&res_pkt, false);
  return res;
}