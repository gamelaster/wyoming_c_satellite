// Copyright 2026 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

#include "satellite_priv.h"

#if 0
static int32_t wsat_mode_packet_handle(enum wsat_packet_type packet_type, struct wsat_packet pkt)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_wake_stream_inst* mode_inst = &inst->mode_inst.wake_stream;

  int32_t res = wsat_packet_default_handle(packet_type, pkt);
  switch (packet_type) {
    case WSAT_EVENT_TYPE_RUN_SATELLITE: {
      mode_inst->is_paused = false;
      res = 1;
      break;
    }
    case WSAT_EVENT_TYPE_PAUSE_SATELLITE: {
      mode_inst->is_paused = false;

      PLAT_MUTEX_LOCK(&inst->is_streaming_mutex);
      inst->is_streaming = false;
      PLAT_MUTEX_UNLOCK(&inst->is_streaming_mutex);
      res = 1;
      break;
    }
    case WSAT_EVENT_TYPE_VOICE_STOPPED: {
      PLAT_MUTEX_LOCK(&inst->is_streaming_mutex);
      inst->is_streaming = false;
      PLAT_MUTEX_UNLOCK(&inst->is_streaming_mutex);
      if (inst->mic && inst->mic->stop_stream_fn) {
        inst->mic->stop_stream_fn();
      }
      res = 1;
      break;
    }
    default:
      break;
  }

  return res;
}

void wsat_mode_wake_stream_trigger_detection()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_wake_stream_inst* mode_inst = &inst->mode_inst.wake_stream;

  if (wsat_server_is_connected() && !mode_inst->is_paused) {
    cJSON* header = cJSON_CreateObject();
    cJSON_AddStringToObject(header, "type", "detection");
    cJSON_AddStringToObject(header, "version", "1.5.2");

    cJSON* data = cJSON_CreateObject();
    // cJSON_AddStringToObject(data, "name", WAKE_MODEL_NAME);
    cJSON_AddNumberToObject(data, "timestamp", 4879521185556); // TODO

    struct wsat_packet res_pkt = {
      .header = header,
      .data = data
    };
    wsat_packet_send(res_pkt);
    wsat_packet_free(res_pkt, false);

    wsat_run_pipeline_send(NULL);

    PLAT_MUTEX_LOCK(&inst->is_streaming_mutex);
    inst->is_streaming = true;
    PLAT_MUTEX_UNLOCK(&inst->is_streaming_mutex);
    if (inst->mic && inst->mic->start_stream_fn) {
      inst->mic->start_stream_fn();
    }
  }
}

static int32_t wsat_mode_init()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_wake_stream_inst* mode_inst = &inst->mode_inst.wake_stream;
  mode_inst->is_paused = true;
  return 0;
}

static int32_t wsat_mode_destroy()
{
  return 0;
}

struct wsat_mode_if wsat_mode_wake_stream = {
  WSAT_MODE_WAKE_STREAM,
  wsat_mode_init,
  wsat_mode_destroy,
  wsat_mode_packet_handle,
};
#endif

static int32_t wsat_mode_init()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_wake_stream_inst* mode_inst = &inst->mode_inst.wake_stream;
  mode_inst->is_streaming = false;
  mode_inst->is_paused = false;
  PLAT_MUTEX_CREATE(&mode_inst->state_mutex); // TODO: Error handling
  return 0;
}

static int32_t wsat_mode_destroy()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_wake_stream_inst* mode_inst = &inst->mode_inst.wake_stream;
  PLAT_MUTEX_DESTROY(&mode_inst->state_mutex);
  return 0;
}

static int32_t wsat_mode_sys_event_handle(enum wsat_sys_event_type type, void* data)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_wake_stream_inst* mode_inst = &inst->mode_inst.wake_stream;

  switch (type) {
  case WSAT_SYS_EVENT_SAT_DISCONNECT: {
    PLAT_MUTEX_LOCK(&mode_inst->state_mutex);
    mode_inst->is_streaming = false;
    mode_inst->is_paused = false;
    PLAT_MUTEX_UNLOCK(&mode_inst->state_mutex);
    break;
  }
  case WSAT_SYS_EVENT_MIC_DATA: {
    PLAT_MUTEX_LOCK(&mode_inst->state_mutex);
    bool is_streaming = mode_inst->is_streaming;
    bool is_paused = mode_inst->is_paused;
    PLAT_MUTEX_UNLOCK(&mode_inst->state_mutex);
    if (is_paused) return 0;
    struct wsat_sys_event_buffer_params* buffer = data;
    if (is_streaming) {
      wsat_audio_chunk_send(buffer->data, buffer->size);
    } else {
      // TODO: Send to wake
    }
    break;
  }
  case WSAT_SYS_EVENT_WAKE_DETECTION: {
    PLAT_MUTEX_LOCK(&mode_inst->state_mutex);
    bool is_streaming = mode_inst->is_streaming;
    bool is_paused = mode_inst->is_paused;
    if (!is_streaming && !is_paused) {
      mode_inst->is_streaming = true;
    }
    PLAT_MUTEX_UNLOCK(&mode_inst->state_mutex);
    if (is_streaming || is_paused) return 0;

    cJSON* header = cJSON_CreateObject();
    cJSON_AddStringToObject(header, "type", "detection");
    cJSON_AddStringToObject(header, "version", "1.5.2");

    cJSON* data_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(data_obj, "name", inst->wake->name);
    cJSON_AddNumberToObject(data_obj, "timestamp", 4879521185556); // TODO

    struct wsat_event res_pkt = {
      .header = header,
      .data = data_obj
    };
    wsat_event_send(&res_pkt);
    wsat_event_free(&res_pkt, false);

    wsat_run_pipeline_send(NULL);

    break;
  }
  default: break;
  }
  return 0;
}

static int32_t wsat_mode_event_handle(enum wsat_packet_type event_type, struct wsat_decoded_event* evt)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_wake_stream_inst* mode_inst = &inst->mode_inst.wake_stream;
  int32_t res = wsat_event_handle_default(event_type, evt);

  // The separate explicit locks are intentional in case some other event will not need lock :)

  switch (event_type) {
  case WSAT_EVENT_TYPE_RUN_SATELLITE: {
    PLAT_MUTEX_LOCK(&mode_inst->state_mutex);
    mode_inst->is_streaming = false;
    mode_inst->is_paused = false;
    PLAT_MUTEX_UNLOCK(&mode_inst->state_mutex);
    res = 1;
    break;
  }
  case WSAT_EVENT_TYPE_PAUSE_SATELLITE: {
    PLAT_MUTEX_LOCK(&mode_inst->state_mutex);
    mode_inst->is_streaming = false;
    mode_inst->is_paused = true;
    PLAT_MUTEX_UNLOCK(&mode_inst->state_mutex);
    res = 1;
    break;
  }
  case WSAT_EVENT_TYPE_TRANSCRIPT:
  case WSAT_EVENT_TYPE_ERROR: {
    PLAT_MUTEX_LOCK(&mode_inst->state_mutex);
    mode_inst->is_streaming = false;
    PLAT_MUTEX_UNLOCK(&mode_inst->state_mutex);
    res = 1;
    break;
  }
  default: break;
  }
  return res;
}

struct wsat_mode wsat_mode_wake_stream = {
  {
    WSAT_COMPONENT_TYPE_MODE,
    wsat_mode_init,
    wsat_mode_destroy,
    wsat_mode_sys_event_handle,
  },
  WSAT_MODE_WAKE_STREAM,
  wsat_mode_event_handle,
};