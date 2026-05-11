// Copyright 2026 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

#include "satellite_priv.h"

static int32_t wsat_mode_init()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_always_stream_inst* mode_inst = &inst->mode_inst.always_stream;
  mode_inst->is_streaming = false;
  PLAT_MUTEX_CREATE(&mode_inst->is_streaming_mutex); // TODO: Error handling
  return 0;
}

static int32_t wsat_mode_destroy()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_always_stream_inst* mode_inst = &inst->mode_inst.always_stream;
  PLAT_MUTEX_DESTROY(&mode_inst->is_streaming_mutex);
  return 0;
}

static int32_t wsat_mode_sys_event_handle(enum wsat_sys_event_type type, void* data)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_always_stream_inst* mode_inst = &inst->mode_inst.always_stream;
  switch (type) {
  case WSAT_SYS_EVENT_MIC_DATA: {
    PLAT_MUTEX_LOCK(&mode_inst->is_streaming_mutex);
    bool is_streaming = mode_inst->is_streaming;
    PLAT_MUTEX_UNLOCK(&mode_inst->is_streaming_mutex);
    if (!is_streaming) return 0;
    struct wsat_sys_event_buffer_params* buffer = data;
    wsat_audio_chunk_send(buffer->data, buffer->size);
    break;
  }
  case WSAT_SYS_EVENT_SAT_DISCONNECT: {
    PLAT_MUTEX_LOCK(&mode_inst->is_streaming_mutex);
    mode_inst->is_streaming = false;
    PLAT_MUTEX_UNLOCK(&mode_inst->is_streaming_mutex);
  }
  default: break;
  }
  return 0;
}

static int32_t wsat_mode_event_handle(enum wsat_packet_type event_type, struct wsat_decoded_event* evt)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_always_stream_inst* mode_inst = &inst->mode_inst.always_stream;
  int32_t res = wsat_event_handle_default(event_type, evt);
  switch (event_type) {
  case WSAT_EVENT_TYPE_RUN_SATELLITE:
    wsat_run_pipeline_send(NULL);
    PLAT_MUTEX_LOCK(&mode_inst->is_streaming_mutex);
    mode_inst->is_streaming = true;
    PLAT_MUTEX_UNLOCK(&mode_inst->is_streaming_mutex);
    res = 1;
    break;
  case WSAT_EVENT_TYPE_PAUSE_SATELLITE:
    PLAT_MUTEX_LOCK(&mode_inst->is_streaming_mutex);
    mode_inst->is_streaming = false;
    PLAT_MUTEX_UNLOCK(&mode_inst->is_streaming_mutex);
    res = 1;
    break;
  default: break;
  }
  return res;
}

struct wsat_mode wsat_mode_always_stream = {
  {
    WSAT_COMPONENT_TYPE_MODE,
    wsat_mode_init,
    wsat_mode_destroy,
    wsat_mode_sys_event_handle,
  },
  WSAT_MODE_ALWAYS_STREAM,
  wsat_mode_event_handle,
};
